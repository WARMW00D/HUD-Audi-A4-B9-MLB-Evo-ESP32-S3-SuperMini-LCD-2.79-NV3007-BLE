#!/usr/bin/env python3
"""
gen_arrow_anim.py — пакует анимированные стрелки навигации (кадры PNG) в arrows.bin для HUD SuperMini.

    pip install pillow numpy lz4
    python tools/gen_arrow_anim.py <папка_с_кадрами> [arrows.bin] [--w 160] [--h 101]

Папка с кадрами: <папка>/large_sport/<имя>.p<N>.png (+ blank.png — пустой фон и frames.idx со строками
«имя<TAB>число_кадров»). Кадр 328x180 (вид с перспективой и сеткой пола): содержимое (дорога, стрелка, кольцо,
флаг) занимает только прямоугольник CROP (общий для всех кадров; границы найдены по всем кадрам), он вырезается
и масштабируется до W x H (W чётная, по высоте кадр сжимается: сверху остаётся место под бар прогресса,
снизу — под строку дистанции).

Пол с сеткой одинаков во всех кадрах (blank.png) и в файл НЕ кладётся: пиксели, совпадающие с полом, прозрачны —
анимация рисуется на чёрном. Кадры — общая палитра 16 цветов (индекс 0 = прозрачный), 4 бита на пиксель,
каждый кадр сжат отдельно блоком LZ4: любой кадр распаковывается независимо (произвольный доступ, прыжки по манёвру).

Файл заливается в HUD тем же путём, что и база камер (приложение HUD Control 1.6+: «Выбрать cams.bin»
принимает и arrows.bin), и лежит во флеше W25Q128 с адреса 0x800000 (HUD_ANIM_FLASH_ADDR).

Формат файла (little-endian):
  0   "ANM1"
  4   u16 W, u16 H
  8   u16 число анимаций, u16 0
  12  u32 размер файла
  16  u32 0 (зарезервировано)
  20  палитра кадров 16 x 4 байта (B, G, R, A) — формат lv_color32_t для LV_IMG_CF_INDEXED_4BIT
  84  таблица анимаций: N x 32 байта: имя[24] (0-дополнено), u16 кадров, u16 0, u32 номер первого кадра
  ... таблица смещений кадров: M x u32 (от начала файла)
  ... кадры: u16 длина сжатого блока + блок LZ4 (распакованный размер = ((W+1)//2)*H)
"""
import argparse, os, struct
import numpy as np
import lz4.block
from PIL import Image

SKIP = {"merge_l", "merge_r", "turn_r000"}   # не используются; «прямо» рисует approach (без скруглённого тупика)
CROP = (68, 39, 260, 180)                    # общая рамка содержимого во всех кадрах 328x180 (проверено по всем кадрам)
SMOOTH = {"approach": 36}                    # «прямо»: исходных кадров мало (13) — досчитываем до N смешиванием соседних
DIFF = 2                                     # пиксель отличается от пола больше, чем на это — рисуется


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("out", nargs="?")
    ap.add_argument("--w", type=int, default=160); ap.add_argument("--h", type=int, default=101)
    a = ap.parse_args()
    W, H = a.w, a.h
    assert W % 2 == 0
    out = a.out or os.path.join(a.src, "arrows.bin")
    d = os.path.join(a.src, "large_sport")

    def fit(im):
        return np.array(im.crop(CROP).resize((W, H), Image.LANCZOS)).astype(np.int32)

    bg = fit(Image.open(os.path.join(d, "blank.png")).convert("RGB"))
    names = []
    for line in open(os.path.join(d, "frames.idx"), encoding="utf-8"):
        p = line.split()
        if len(p) == 2 and p[0] not in SKIP:
            names.append((p[0], int(p[1])))

    def load(n, i):
        return fit(Image.open(f"{d}/{n}.p{i}.png").convert("RGB"))

    def mask_of(c):
        return np.abs(c - bg).max(2) > DIFF

    def blend(n, k, j, kk):                      # кадр j из kk: линейная интерполяция по исходным k кадрам
        t = j * (k - 1) / (kk - 1)
        i = min(int(t), k - 2); f = t - i
        return np.rint(load(n, i) * (1 - f) + load(n, i + 1) * f).astype(np.int32)

    pix = []
    for n, k in names:
        for i in range(0, k, 2):
            c = load(n, i)
            pix.append(c[mask_of(c)][::2])
    pix = np.concatenate(pix).astype(np.uint8)
    side = int(len(pix) ** 0.5)
    pim = Image.fromarray(pix[: side * side].reshape(side, side, 3))
    pal15 = np.array(pim.quantize(15, method=Image.Quantize.MEDIANCUT, kmeans=3).getpalette()[:45]).reshape(15, 3)
    pal = np.vstack([[0, 0, 0], pal15]).astype(np.int32)

    def pack(idx):
        f = idx.reshape(-1)
        return ((f[0::2] << 4) | f[1::2]).astype(np.uint8).tobytes()           # старший ниббл — левый пиксель

    def lz(b):
        blk = lz4.block.compress(b, mode="high_compression", compression=12, store_size=False)
        assert len(blk) < 65536
        return struct.pack("<H", len(blk)) + blk

    frames, anim = [], []
    for n, k in names:
        kk = SMOOTH.get(n, k)
        anim.append((n, kk, len(frames)))
        for i in range(kk):
            c = blend(n, k, i, kk) if kk != k else load(n, i)
            dist = ((c[:, :, None, :] - pal[None, None, :, :]) ** 2).sum(3)
            idx = dist.argmin(2).astype(np.uint8)
            idx[~mask_of(c)] = 0
            frames.append(lz(pack(idx)))

    head = 20 + 64
    offs_at = head + len(anim) * 32
    o = offs_at + len(frames) * 4
    offs = []
    for f in frames:
        offs.append(o)
        o += len(f)
    size = o
    b = bytearray()
    b += b"ANM1" + struct.pack("<HHHHII", W, H, len(anim), 0, size, 0)
    for r, g, bl in pal:
        b += bytes((int(bl), int(g), int(r), 0 if (r == 0 and g == 0 and bl == 0) else 255))
    for n, k, first in anim:
        b += n.encode().ljust(24, b"\0") + struct.pack("<HHI", k, 0, first)
    for x in offs:
        b += struct.pack("<I", x)
    for f in frames:
        b += f
    assert len(b) == size
    open(out, "wb").write(b)
    print(f"{out}: {W}x{H}, {len(anim)} анимаций, {len(frames)} кадров, {size / 1024:.0f} КБ")


if __name__ == "__main__":
    main()
