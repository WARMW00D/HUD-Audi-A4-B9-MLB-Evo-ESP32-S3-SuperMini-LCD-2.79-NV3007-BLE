# BLE OTA — HUD v29.0 / Android app 1.2

[Русская версия](OTA_ru.md)

## Initial migration: USB required

v28.x uses one factory application partition. OTA cannot replace the partition table. Install **v29.0 with the supplied `supermini_hud/partitions.csv` over USB** before using phone updates.

Open `supermini_hud/supermini_hud.ino`; keep your working board configuration (4 MB Flash, 2 MB QSPI PSRAM). Set **Erase All Flash = Disabled**. The initial USB upload must write the bootloader, new partition table and app. Check the build/upload output: it must use this sketch’s CSV and write the table at 0x8000. Flashing only an application `.bin` does not migrate the layout.

| Partition | Offset | Size |
|---|---:|---:|
| NVS | 0x9000 | 0x5000 / 20 KiB |
| otadata | 0xE000 | 0x2000 / 8 KiB |
| app0 | 0x10000 | 0x1F0000 / 1,984 KiB |
| app1 | 0x200000 | 0x1F0000 / 1,984 KiB |
| coredump | 0x3F0000 | 0x10000 / 64 KiB |

Maximum application image: **2,031,616 bytes**. Check the new firmware size after compilation. NVS offset and size are unchanged, so settings and bonding keys are intended to survive a non-erasing USB upload; verify this on the device. Do not reset phone ownership just to install OTA.

At Serial 115200, expect:

```text
[fw] HUD Super Mini v29.0 / BLE OTA
[ota] BLE service ready, next slot capacity=2031616
```

Capacity 0 indicates no usable OTA layout. The firmware sends Service Changed when the GATT schema is first upgraded; Android may ask you to reconnect. If the OTA service remains absent, toggle Android Bluetooth, restart the HUD and reconnect before considering a bond reset.

## Update the Android app

Build app 1.2 from [the Android repository](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App). Install over the old app with the same application ID and signing key. Keep the existing Android project’s wrapper/local SDK configuration if replacing only `app/`. Use Gradle 8.9 / AGP 8.7.3 and an ASCII-only Windows path.

## Select the right image

Arduino IDE: Sketch → **Export Compiled Binary**. Choose **`supermini_hud.ino.bin`**, the application image built for this HUD. Do not select `*.bootloader.bin`, `*.partitions.bin`, `*.merged.bin` or a complete flash dump.

Future firmware must retain the compatible two-slot layout and OTA service. BLE OTA changes the application, not bootloader, partition table, eFuses or board hardware parameters. A valid ESP32-S3 header alone does not prove that an arbitrary firmware will work with this display or project.

## Transfer

1. Disconnect other BLE clients such as nRF Connect. Connect through HUD Control using the owner phone.
2. Open **Firmware update** and choose the application `.bin`.
3. The app checks the size/header and computes SHA-256.
4. Select **Update HUD**, check the filename and confirm.
5. Keep the app open, phone nearby and power stable. A transfer can take several minutes.
6. At 100%, wait: all bytes have arrived, but verification/commit may still be pending.
7. The HUD checks SHA-256 and validates the ESP-IDF image before choosing the boot slot.
8. After commit the HUD reboots. Reconnect and check graphics, settings and CAN data.

For the first test, you can transfer the same compiled v29.0 app into the other slot. Verify normal operation before relying on subsequent OTA updates.

## Cancellation, faults and recovery

Cancel is available before boot-slot commit. Cancel, disconnect, invalid hash/image or timeout before commit leaves the old boot slot selected. Resume is not implemented: start the next transfer from offset zero.

Firmware inactivity timeout: **120 s**. Android timeout per BLE operation: **30 s**. The HUD schedules reboot **2 s** after successful commit even if the final reply is lost.

If COMMIT acknowledgment is lost, the app reports an uncertain result: the slot may already have changed. Wait for reboot and check the HUD before retransmitting. Duplicate commit is idempotent; a late abort cannot undo a committed update.

| Error | Meaning |
|---:|---|
| 1 | Invalid command/packet |
| 2 | No suitable OTA partition layout |
| 3 | Size mismatch or image too large |
| 4 | Flash / SDK / boot selection failure |
| 5 | Wrong, skipped or repeated offset |
| 6 | SHA-256 mismatch |
| 7 | Invalid image, chip or ESP-IDF validation failure |
| 8 | Command in an invalid state |
| 9 | Session timeout |

Read/write access requires encrypted, authenticated bonding and saved owner identity. SHA-256 is an integrity check, **not a firmware signature**. Runtime rollback of a new application that passes image validation but malfunctions is not guaranteed by this implementation; USB remains the recovery path.

## Protocol v1

| Role | UUID |
|---|---|
| Service | `74d0a200-3d92-4f50-9b1a-478142000001` |
| Control READ / WRITE with response | `74d0a200-3d92-4f50-9b1a-478142000002` |
| Data WRITE with response | `74d0a200-3d92-4f50-9b1a-478142000003` |

No notifications. Integers are little-endian.

| Packet | Content |
|---|---|
| START | `01 + uint32(size) + SHA256[32]`, 37 bytes |
| Data | `uint32(offset) + 1…240 payload bytes`; first payload ≥36 bytes |
| END | `02`: verify complete image |
| COMMIT | `03`: choose next boot slot |
| ABORT | `04`: cancel an uncommitted session |

Control read returns 16 bytes: protocol version 1, state, error, reserved zero, then `uint32(received)`, `uint32(total)`, `uint32(capacity)`. States: Idle 0, Receiving 1, Verified 2, Committed 3, Error 4.

The app negotiates MTU (minimum 64), sends one operation at a time and checks the confirmed offset after each chunk. Ordinary settings are blocked in the app during OTA; changing the app language is allowed.

## Validation status

Host tests use real SHA-256 through OpenSSL and mocked NimBLE/ESP-IDF. They cover transfer, owner checks, commit-after-verification, lost final ACK/reboot, duplicate commit, header/chip/hash rejection, flash faults, offset/size failures, incomplete image, cancel/disconnect and timeout. Settings/NVS regression tests pass; mbedTLS 2/3 API branches were checked with host adapters.

**The ESP32/Android builds and real-device OTA transfer have not been performed in the authoring environment.** Real ESP-IDF image validation, GATT cache refresh, Flash behaviour and migration of bonds need device testing; host stubs do not verify those.

API references: [ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html), [NimBLE services](https://h2zero.github.io/NimBLE-Arduino/class_nim_b_l_e_service.html), [NimBLE server](https://h2zero.github.io/NimBLE-Arduino/class_nim_b_l_e_server.html), [Android BluetoothGatt](https://developer.android.com/reference/android/bluetooth/BluetoothGatt).
