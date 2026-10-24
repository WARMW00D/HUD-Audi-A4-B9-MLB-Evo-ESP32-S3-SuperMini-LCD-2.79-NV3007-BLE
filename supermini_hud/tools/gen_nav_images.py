#!/usr/bin/env python3
"""
gen_nav_images.py — стрелки манёвров навигации для HUD (LV_IMG_CF_ALPHA_4BIT)
------------------------------------------------------------------------------
Рисует стрелки по MainElement + Direction из BAP Navigation_SD:
  - поворот (Turn / PrepareTurn / неизвестный элемент) — 16 направлений по 22.5°
  - кольцо (Roundabout) — 16 направлений съезда, движение против часовой (правостороннее)
  - прямо, съезд с магистрали L/R, развилка L/R, разворот L/R, финиш
Яркость = альфа (фон чёрный): стрелка — полная яркость, дорога-контекст — серая.
Цвет задаётся в прошивке через img_recolor.

Direction: 0x00 прямо, 0x40 налево, 0x80 назад, 0xC0 направо (360/256 град,
против часовой). Сектор = ((dir + 8) >> 4) & 15.

Выход: hud_nav_images.c / hud_nav_images.h + tools/preview_nav.png
"""
import math, os
from PIL import Image, ImageDraw, ImageChops, ImageFilter

S = 8
HUD_PANEL_W, HUD_LAYOUT_W = 428, 518     # физическая ширина панели / ширина раскладки (user_config.h)
W, H = 192, 116                 # область стрелки (ниже — строка дистанции)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

ARROW_W = 15                    # толщина стрелки
HEAD_W, HEAD_L = 36, 22         # наконечник
ROAD_W = 22                     # дорога-контекст
ROAD_A = 70                     # яркость дороги
ARROW_A = 255
ROAD_FILL = 100
ROAD_BORDER = 2
SKIP = 1
CUT_A = 0
SHADOW = 3
SHADOW_K = 0.6
CX, JY, BY = W / 2, 60, H - 4   # центр, перекрёсток, низ


class SD(ImageDraw.ImageDraw):
    """Пропускает стрелочные фигуры при построении дорожного слоя."""
    def _skip(self, k): return k.get("fill") == SKIP
    def line(self, *a, **k):
        if not self._skip(k): super().line(*a, **k)
    def ellipse(self, *a, **k):
        if not self._skip(k): super().ellipse(*a, **k)
    def polygon(self, *a, **k):
        if not self._skip(k): super().polygon(*a, **k)
    def rectangle(self, *a, **k):
        if not self._skip(k): super().rectangle(*a, **k)
    def arc(self, *a, **k):
        if not self._skip(k): super().arc(*a, **k)

def cv():
    return Image.new("L", (W * S, H * S), 0)

def P(pts):
    return [(x * S, y * S) for x, y in pts]

def vec(angle_deg):
    """Угол против часовой от 'вверх' -> вектор в экранных координатах."""
    a = math.radians(angle_deg)
    return -math.sin(a), -math.cos(a)

FADE_L = 26                     # длина растворения дороги у открытого конца (px, область 192x116)
FADES = []                      # открытые концы дорог текущего манёвра: (x, y, vx, vy, длина)


def line(d, pts, w, fill=None, fade=False):
    """Дорога-контекст. fade=True: у последней точки вместо скругления — растворение.
    Растворение заканчивается там, где раньше заканчивалось скругление
    (на радиус w/2 дальше конца оси, но не дальше края области)."""
    if fill is None: fill = ARROW_A
    r = w / 2
    ends = (pts[0],) if fade else (pts[0], pts[-1])
    d.line(P(pts), fill=fill, width=int(w * S), joint="curve")
    for x, y in ends:
        d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=fill)
    if fade and fill != SKIP:
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        L = math.hypot(x2 - x1, y2 - y1)
        vx, vy = (x2 - x1) / L, (y2 - y1) / L
        # дорога продолжается до края скругления, чтобы растворение дошло до нуля
        ext = r
        d.line(P([(x2, y2), (x2 + vx * ext, y2 + vy * ext)]), fill=fill, width=int(w * S))
        tmax = ext
        for c0, dv, hi in ((x2, vx, W), (y2, vy, H)):
            if dv > 1e-6: tmax = min(tmax, (hi - c0) / dv)
            if dv < -1e-6: tmax = min(tmax, (0 - c0) / dv)
        FADES.append((x2, y2, vx, vy, max(tmax, 0.0)))

def head(d, tip, v, fill=None):
    if fill is None: fill = ARROW_A
    vx, vy = v
    nx, ny = -vy, vx
    bx, by = tip[0] - vx * HEAD_L, tip[1] - vy * HEAD_L
    d.polygon(P([tip, (bx + nx * HEAD_W / 2, by + ny * HEAD_W / 2), (bx - nx * HEAD_W / 2, by - ny * HEAD_W / 2)]), fill=fill)

def arrow_path(d, pts):
    """Ломаная со скруглёнными стыками, последний отрезок — к наконечнику."""
    (x1, y1), (x2, y2) = pts[-2], pts[-1]
    L = math.hypot(x2 - x1, y2 - y1)
    v = ((x2 - x1) / L, (y2 - y1) / L)
    body_end = (x2 - v[0] * (HEAD_L - 2), y2 - v[1] * (HEAD_L - 2))
    d.line(P(pts[:-1] + [body_end]), fill=ARROW_A, width=int(ARROW_W * S), joint="curve")
    r = ARROW_W / 2
    x0, y0 = pts[0]
    d.ellipse([(x0 - r) * S, (y0 - r) * S, (x0 + r) * S, (y0 + r) * S], fill=ARROW_A)
    for (px, py) in pts[1:-1]:
        d.ellipse([(px - r) * S, (py - r) * S, (px + r) * S, (py + r) * S], fill=ARROW_A)
    head(d, pts[-1], v)

def fit_len(ox, oy, v, margin=4):
    """Длина луча из (ox,oy) по v до края области (с запасом под наконечник)."""
    ts = []
    for c0, dv, lo, hi in ((ox, v[0], margin + HEAD_W / 2, W - margin - HEAD_W / 2),
                           (oy, v[1], margin, H - margin)):
        if dv > 1e-6:  ts.append((hi - c0) / dv)
        if dv < -1e-6: ts.append((lo - c0) / dv)
    return min(ts) if ts else 60

# ------------------------------------------------------------------ манёвры ---
def img_turn(sector):
    ang = sector * 22.5
    im = cv(); d = SD(im)
    v = vec(ang)
    # дорога: прямо через перекрёсток + выбранное направление
    line(d, [(CX, BY + 6), (CX, 4)], ROAD_W, ROAD_A, fade=True)
    Lr = min(fit_len(CX, JY, v, 0), 70)
    line(d, [(CX, JY), (CX + v[0] * Lr, JY + v[1] * Lr)], ROAD_W, ROAD_A, fade=True)
    if sector == 0:
        arrow_path(d, [(CX, BY), (CX, 8)])
    elif sector == 8:
        del FADES[:]
        return img_uturn(left=True)
    else:
        # крутые повороты: 7/9 (157.5 град) рисуем разворотом, 6/10 (135 град) —
        # со стойкой, смещённой в сторону от обратного луча
        if sector in (7, 9):
            del FADES[:]
            return img_uturn(left=(sector == 7))
        if sector in (6, 10):
            sx = 1 if sector == 6 else -1
            x0, jy = CX + sx * 24, 34
            line(d, [(x0, jy), (x0 + v[0] * 70, jy + v[1] * 70)], ROAD_W, ROAD_A, fade=True)
            line(d, [(x0, BY + 6), (x0, 4)], ROAD_W, ROAD_A, fade=True)
            arrow_path(d, [(x0, BY), (x0, jy), (x0 + v[0] * 66, jy + v[1] * 66)])
        else:
            L = min(fit_len(CX, JY, v), 58)
            arrow_path(d, [(CX, BY), (CX, JY), (CX + v[0] * L, JY + v[1] * L)])
    return im

def img_uturn(left=True):
    im = cv(); d = SD(im)
    s = -1 if left else 1
    R = 20
    top = 30
    x2 = CX + s * 2 * R
    line(d, [(CX, BY + 6), (CX, 4)], ROAD_W, ROAD_A, fade=True)
    line(d, [(x2, BY + 6), (x2, 4)], ROAD_W, ROAD_A, fade=True)
    # стойка вверх, дуга, вниз
    d.line(P([(CX, BY), (CX, top + R)]), fill=ARROW_A, width=int(ARROW_W * S))
    r = ARROW_W / 2
    d.ellipse([(CX - r) * S, (BY - r) * S, (CX + r) * S, (BY + r) * S], fill=ARROW_A)
    cx = CX + s * R
    box = [(cx - R - ARROW_W / 2) * S, (top - ARROW_W / 2) * S, (cx + R + ARROW_W / 2) * S, (top + 2 * R + ARROW_W / 2) * S]
    d.ellipse(box, fill=ARROW_A)
    inner = [(cx - R + ARROW_W / 2) * S, (top + ARROW_W / 2) * S, (cx + R - ARROW_W / 2) * S, (top + 2 * R - ARROW_W / 2) * S]
    d.ellipse(inner, fill=CUT_A)
    d.rectangle([(cx - R - ARROW_W) * S, (top + R) * S, (cx + R + ARROW_W) * S, (top + 2 * R + ARROW_W) * S], fill=CUT_A)
    d.line(P([(CX, top + R), (CX, BY)]), fill=ARROW_A, width=int(ARROW_W * S))
    d.ellipse([(CX - r) * S, (BY - r) * S, (CX + r) * S, (BY + r) * S], fill=ARROW_A)
    tip_y = 86
    d.line(P([(x2, top + R), (x2, tip_y - HEAD_L + 2)]), fill=ARROW_A, width=int(ARROW_W * S))
    head(d, (x2, tip_y), (0, 1))
    return im

def img_roundabout(sector):
    ang = sector * 22.5
    im = cv(); d = SD(im)
    rc, R = (CX, 46), 29
    RW = 11                                   # толщина стрелки на кольце
    # все съезды серым: 4 основных луча + кольцо
    d.ellipse([(rc[0] - R - ROAD_W / 2) * S, (rc[1] - R - ROAD_W / 2) * S,
               (rc[0] + R + ROAD_W / 2) * S, (rc[1] + R + ROAD_W / 2) * S], fill=ROAD_A)
    d.ellipse([(rc[0] - R + ROAD_W / 2) * S, (rc[1] - R + ROAD_W / 2) * S,
               (rc[0] + R - ROAD_W / 2) * S, (rc[1] + R - ROAD_W / 2) * S], fill=0)
    line(d, [(CX, BY + 6), (CX, rc[1] + R)], ROAD_W, ROAD_A)
    v = vec(ang)
    ex0 = (rc[0] + v[0] * R, rc[1] + v[1] * R)
    Le = min(fit_len(ex0[0], ex0[1], v), 30)
    line(d, [ex0, (ex0[0] + v[0] * (Le + 6), ex0[1] + v[1] * (Le + 6))], ROAD_W, ROAD_A, fade=True)
    # путь стрелки: въезд снизу, по кольцу против часовой (визуально), съезд
    entry = (CX, rc[1] + R)
    d.line(P([(CX, BY), entry]), fill=ARROW_A, width=int(ARROW_W * S))
    r = ARROW_W / 2
    d.ellipse([(CX - r) * S, (BY - r) * S, (CX + r) * S, (BY + r) * S], fill=ARROW_A)
    # экранный угол PIL: 0 = вправо (3 ч), растёт по часовой. Въезд = 90 (6 ч).
    # Выход в точке по направлению ang: экранный угол = 270 - ang (против часовой => уменьшаем)
    start, end = 90, (270 - ang) % 360
    # движение визуально против часовой: от 90 вниз к 0 и дальше -> дуга [end, start] по часовой в PIL
    span_start, span_end = end, start
    if span_start >= span_end: span_start -= 360
    if ang % 360 == 180: span_start, span_end = -270, 90   # полный круг (разворот)
    box = [(rc[0] - R) * S, (rc[1] - R) * S, (rc[0] + R) * S, (rc[1] + R) * S]
    box = [(rc[0] - R - RW / 2) * S, (rc[1] - R - RW / 2) * S, (rc[0] + R + RW / 2) * S, (rc[1] + R + RW / 2) * S]
    d.arc(box, span_start, span_end, fill=ARROW_A, width=int(RW * S))
    for a in (span_start, span_end):
        ar = math.radians(a)
        px, py = rc[0] + R * math.cos(ar), rc[1] + R * math.sin(ar)
        d.ellipse([(px - RW / 2) * S, (py - RW / 2) * S, (px + RW / 2) * S, (py + RW / 2) * S], fill=ARROW_A)
    tip = (ex0[0] + v[0] * Le, ex0[1] + v[1] * Le)
    body_end = (tip[0] - v[0] * (HEAD_L - 2), tip[1] - v[1] * (HEAD_L - 2))
    d.line(P([ex0, body_end]), fill=ARROW_A, width=int(ARROW_W * S))
    head(d, tip, v)
    return im

def img_exit(right=True):
    im = cv(); d = SD(im)
    s = 1 if right else -1
    xm = CX - s * 14
    line(d, [(xm, BY + 6), (xm, 4)], ROAD_W, ROAD_A, fade=True)
    pts = [(xm, BY), (xm, 70), (xm + s * 30, 38), (xm + s * 44, 12)]
    line(d, [(xm, 70), (xm + s * 30, 38), (xm + s * 44, 4)], ROAD_W, ROAD_A, fade=True)
    arrow_path(d, pts)
    return im

def img_fork(right=True):
    im = cv(); d = SD(im)
    s = 1 if right else -1
    line(d, [(CX, BY + 6), (CX, 64), (CX - 36, 24), (CX - 44, 4)], ROAD_W, ROAD_A, fade=True)
    line(d, [(CX, 64), (CX + 36, 24), (CX + 44, 4)], ROAD_W, ROAD_A, fade=True)
    arrow_path(d, [(CX, BY), (CX, 64), (CX + s * 34, 26), (CX + s * 42, 8)])
    return im

def img_straight():
    return img_turn(0)

def img_arrived():
    im = cv(); d = SD(im)
    # метка-капля с отверстием
    cx, cy, r = CX, 42, 24
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=ARROW_A)
    d.polygon(P([(cx - r * 0.82, cy + r * 0.55), (cx + r * 0.82, cy + r * 0.55), (cx, cy + r + 30)]), fill=ARROW_A)
    ri = 9
    d.ellipse([(cx - ri) * S, (cy - ri) * S, (cx + ri) * S, (cy + ri) * S], fill=0)
    d.ellipse([(cx - 30) * S, (BY - 10) * S, (cx + 30) * S, (BY + 2) * S], outline=ROAD_A, width=3 * S)
    return im

# ------------------------------------------------------------------ вывод -----
def down(im):
    return im.resize((W, H), Image.LANCZOS)

def crop(im):
    bb = im.getbbox()
    return (im.crop(bb), bb[0], bb[1]) if bb else (im, 0, 0)

def fade_mask(q, fades):
    """Множитель 0..255 на сетке q-кратного разрешения: у каждого открытого конца дороги
    плавное растворение длиной FADE_L, ноль — на конце (см. line(fade=True))."""
    import numpy as np
    yy, xx = np.mgrid[0:H * q, 0:W * q]
    px, py = (xx + 0.5) / q, (yy + 0.5) / q
    m = np.ones((H * q, W * q), dtype=np.float32)
    for ex, ey, vx, vy, tmax in fades:
        u = (px - ex) * vx + (py - ey) * vy            # вдоль дороги, 0 = конец оси
        perp = np.abs(-(px - ex) * vy + (py - ey) * vx)
        f = np.clip((tmax - u) / FADE_L, 0.0, 1.0)
        f = f * f * (3 - 2 * f)                         # smoothstep
        zone = (perp <= ROAD_W / 2 + ROAD_BORDER + 2) & (u > tmax - FADE_L - 1)
        m = np.where(zone, np.minimum(m, f), m)
    return Image.fromarray((m * 255 + 0.5).astype(np.uint8), "L")


def road_layer(road, arrow, fades=()):
    q = 4
    u = road.resize((W * q, H * q), Image.LANCZOS)
    m = u.point(lambda v: 255 if v >= 40 else 0)
    dil = m.filter(ImageFilter.MaxFilter(2 * ROAD_BORDER * q + 1))
    border = ImageChops.subtract(dil, m)
    fill = m.point(lambda v: ROAD_FILL if v else 0)
    sh = arrow.resize((W * q, H * q), Image.LANCZOS)
    sh = ImageChops.offset(sh, 0, SHADOW * q).filter(ImageFilter.GaussianBlur(1.2 * q))
    shade = sh.point(lambda v: int(255 - SHADOW_K * v))
    fm_ = fade_mask(q, fades) if fades else None
    def fin(layer):
        o = ImageChops.multiply(layer, shade)
        if fm_ is not None: o = ImageChops.multiply(o, fm_)
        return o.resize((W, H), Image.LANCZOS)
    # два слоя: контур (виден всегда) и заливка (в прошивке открывается снизу вверх по мере приближения)
    return fin(fill), fin(border)

def pack4(im):
    w, h = im.size
    px = im.tobytes()
    out = bytearray()
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        for x in range(0, w, 2):
            a = (row[x] + 8) // 17
            b = (row[x + 1] + 8) // 17 if x + 1 < w else 0
            out.append((min(a, 15) << 4) | min(b, 15))
    return bytes(out)

SW, SH = 25, 15                 # маленькая стрелка "следующий манёвр" (1/5)

def main():
    global ROAD_A, ARROW_A
    imgs = []
    roads = {}
    outs = {}

    def build(prefix, size, round_road=None, road_layers=False):
        global ROAD_A, ARROW_A
        def add(name, fn, *args, road=True):
            global ROAD_A, ARROW_A, CUT_A
            saved = ROAD_A
            ARROW_A = 255
            if road_layers:
                ROAD_A = 0
            big = fn(*args)
            if road_layers and road:
                ROAD_A = ROAD_FILL; ARROW_A = SKIP; CUT_A = SKIP
                del FADES[:]
                f_, o_ = road_layer(fn(*args), big, list(FADES))
                roads[prefix + name] = crop(f_.resize(size, Image.LANCZOS))
                outs[prefix + name] = crop(o_.resize(size, Image.LANCZOS))
            ROAD_A = saved; ARROW_A = 255; CUT_A = 0
            c, x, y = crop(big.resize(size, Image.LANCZOS))
            imgs.append((prefix + name, c, x, y))
        for s_ in range(16): add(f"turn_{s_:02d}", img_turn, s_)
        saved = ROAD_A
        if round_road is not None: ROAD_A = round_road
        for s_ in range(16): add(f"round_{s_:02d}", img_roundabout, s_)
        ROAD_A = saved
        add("exit_l", img_exit, False); add("exit_r", img_exit, True)
        add("fork_l", img_fork, False); add("fork_r", img_fork, True)
        add("uturn_l", img_uturn, True); add("uturn_r", img_uturn, False)
        add("arrived", img_arrived, road=False)

    build("nav_", (192, 116), road_layers=True)    # v31: родной размер Waveshare (192x116), место освободил бар дистанции
    imgs[:] = [(n, im, x, y + 19) for n, im, x, y in imgs]
    # Road coordinates use the same vertical offset as the main images.
    roads = {n: (im, x, y + 19) for n, (im, x, y) in roads.items()}
    outs = {n: (im, x, y + 19) for n, (im, x, y) in outs.items()}
    road_saved = ROAD_A
    ROAD_A = 0
    build("nav_s_", (SW, SH), round_road=110)
    ROAD_A = road_saved

    # Прошивка рисует нативно в 428x142, а координаты (x, y) в таблицах остаются в единицах
    # раскладки 518x172 (hud_native_x/y их переводят): сами картинки хранятся уже в
    # физических пикселях, т.е. уменьшены в 428/518. Смещения не трогаем.
    k = HUD_PANEL_W / HUD_LAYOUT_W
    def phys(im):
        return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    roads = {n: (phys(im), x, y) for n, (im, x, y) in roads.items()}
    outs = {n: (phys(im), x, y) for n, (im, x, y) in outs.items()}
    imgs[:] = [(n, phys(im), x, y) for n, im, x, y in imgs]

    total = 0
    with open(os.path.join(OUT, "hud_nav_images.c"), "w") as f:
        f.write("/* Сгенерировано tools/gen_nav_images.py — не править вручную */\n")
        f.write('#define LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#include "hud_nav_images.h"\n\n')
        for n, (im, x, y) in outs.items():
            name = "roado_" + n; data = pack4(im); total += len(data)
            rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
            f.write(f"static const uint8_t {name}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
            f.write(f"static const lv_img_dsc_t img_{name} = {{\n    .header.cf = LV_IMG_CF_ALPHA_4BIT, .header.always_zero = 0, .header.reserved = 0,\n    .header.w = {im.width}, .header.h = {im.height}, .data_size = {len(data)}, .data = {name}_map,\n}};\n\n")
        for n, (im, x, y) in roads.items():
            name = "road_" + n; data = pack4(im); total += len(data)
            rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
            f.write(f"static const uint8_t {name}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
            f.write(f"static const lv_img_dsc_t img_{name} = {{\n    .header.cf = LV_IMG_CF_ALPHA_4BIT, .header.always_zero = 0, .header.reserved = 0,\n    .header.w = {im.width}, .header.h = {im.height}, .data_size = {len(data)}, .data = {name}_map,\n}};\n\n")
        for n, im, x, y in imgs:
            data = pack4(im); total += len(data)
            rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
            f.write(f"static const uint8_t {n}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
            f.write(f"static const lv_img_dsc_t img_{n} = {{\n    .header.cf = LV_IMG_CF_ALPHA_4BIT, .header.always_zero = 0, .header.reserved = 0,\n    .header.w = {im.width}, .header.h = {im.height}, .data_size = {len(data)}, .data = {n}_map,\n}};\n\n")
        def tbl(name, keys):
            f.write(f"static const NavImg {name}[{len(keys)}] = {{\n")
            for k in keys:
                n, im, x, y = next(t for t in imgs if t[0] == k)
                r = roads.get(n); o = outs.get(n)
                f.write(f"    {{ &img_{n}, {x}, {y}, &img_road_{n}, {r[1]}, {r[2]}, &img_roado_{n}, {o[1]}, {o[2]} }},\n" if r else f"    {{ &img_{n}, {x}, {y}, NULL, 0, 0, NULL, 0, 0 }},\n")
            f.write("};\n")
        for pre in ("nav_", "nav_s_"):
            tbl(pre + "turn", [f"{pre}turn_{s_:02d}" for s_ in range(16)])
            tbl(pre + "round", [f"{pre}round_{s_:02d}" for s_ in range(16)])
            tbl(pre + "exit", [pre + "exit_l", pre + "exit_r"])
            tbl(pre + "fork", [pre + "fork_l", pre + "fork_r"])
            tbl(pre + "uturn", [pre + "uturn_l", pre + "uturn_r"])
            tbl(pre + "arrived", [pre + "arrived"])
        f.write("\nconst NavSet nav_big = { nav_turn, nav_round, nav_exit, nav_fork, nav_uturn, nav_arrived };\n")
        f.write("const NavSet nav_small = { nav_s_turn, nav_s_round, nav_s_exit, nav_s_fork, nav_s_uturn, nav_s_arrived };\n")
    print(f"стрелок {len(imgs)}, данных {total} байт")


if __name__ == "__main__":
    main()
