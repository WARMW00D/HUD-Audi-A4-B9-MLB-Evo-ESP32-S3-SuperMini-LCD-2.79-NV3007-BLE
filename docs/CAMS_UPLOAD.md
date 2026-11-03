# Uploading the speed-camera database (cams.bin) from the phone

Since v31.0.0 the camera database can be updated from the HUD Control app (1.5.0+) over BLE. No programmer is needed.

1. Build the database on the PC with `gen_cams.py` (a `cams_*.bin` file with the `CAM1` header). The `cams.idx` file is **not used** by the HUD: records are sorted inside `cams.bin` and looked up by binary search directly in PSRAM.
2. Copy the file to the phone, connect to the HUD in the app, section "Speed camera database" → choose `cams.bin` → upload.
3. The HUD erases W25Q128 sectors, receives the data, checks SHA-256, writes the first page (the `CAM1` signature) last, reloads the database from flash into PSRAM and checks SHA-256 again. No restart is needed.

Notes: the size limit is free PSRAM (2 MB on the SuperMini) and flash capacity. An interrupted upload leaves the flash without a signature (cameras off after a reboot, log line `no CAM1 database`), never a half-written database, and can simply be repeated; the copy already running in PSRAM is untouched until COMMIT. Cameras are off for 1–3 s during COMMIT. Access rules are the same as for OTA (bonded, authenticated owner).

Speed: data is streamed with Write Without Response in windows of 8 packets; the app reads the status after each window and resumes from the HUD's confirmed `received` offset if a packet was lost (the HUD ignores gaps and duplicates). The connection interval is 7.5–15 ms during the transfer.

Protocol (v1, like OTA): service `74d0a200-3d92-4f50-9b1a-478142000011`, control `…0012`, data `…0013` (WRITE + WRITE_NR). Control: START `01 + size(4) + SHA-256(32)`, END `02`, COMMIT `03`, CANCEL `04`. Data: `offset(4) + up to 240 bytes`. 16-byte status `[1, state, error, 0, received(4), total(4), max size(4)]`. The first packet must begin with `CAM1` (file size `16 + 12 × N`) or `ANM1` (file size equal to the field at offset 12).

## Animated navigation arrows (arrows.bin)

The same upload also takes `arrows.bin` (header `ANM1`, app 1.6.0+): animated navigation arrows. Build it with `supermini_hud/tools/gen_arrow_anim.py <frames folder> arrows.bin` (needs `pillow numpy lz4`; the Waveshare frame set, `large_sport` variant). The HUD tells the file type from the first page:

- `CAM1` goes to flash address 0 (camera database, as above);
- `ANM1` goes to `0x800000` (`HUD_ANIM_FLASH_ADDR`); the file size is stored in the header at offset 12 and must match. The file is about 2.5 MB; the limit is the flash capacity above `0x800000`.

After COMMIT the HUD checks SHA-256 in flash and opens the file without restart (`[anim] arrows.bin loaded: 160x101, ...` in Serial). Only the headers and tables (about 8 KB) stay in RAM; each frame is read from flash and decoded as needed. Without `arrows.bin` the usual arrows are drawn. Both files are independent: uploading one does not touch the other. All frames of the current manoeuvre loop every 2 s (`HUD_ANIM_PERIOD_MS`); the progress bar is drawn above the animation and the distance below.
