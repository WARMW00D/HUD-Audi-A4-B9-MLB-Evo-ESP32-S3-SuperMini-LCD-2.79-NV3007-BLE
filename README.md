# HUD for Audi A4 B9 (MLB-Evo) — ESP32-S3 Super Mini / NV3007 2.79″ / BLE


[Русская версия](README_ru.md) · [Android companion app](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App)


A DIY head-up display showing vehicle data from Audi I-CAN: speed, gear, ACC, speed limiter, driver assistance, traffic signs, doors and navigation. This is the compact **428 × 142 NV3007** adaptation of the [Waveshare 3.49″ project](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Waveshare-ESP32-S3-Touch-LCD-3.49), with Android settings and firmware updates over BLE.


**Verified vehicle configuration:** operation was checked on an **Audi A4 B9 facelift (restyling) with MIB3 High infotainment**.


**Firmware: v29.1.9.9. Android app: 1.4.1.** This repository contains firmware source and wiring diagrams. Android source lives in its own repository. Prebuilt v29.1.9.9 app firmware and Android 1.4.1 APKs are available in Releases below.


## Tank capacity, acceleration bar and overspeed tolerance


Source versions: **HUD firmware v29.1.9.9 / Android app 1.4.1 (versionCode 5)**.


- **Tank capacity:** enter a whole number from **1 to 200 litres**; default **54 L**. The capacity is always entered in litres, even with US gallons selected, and updates the HUD fuel-to-add calculation.
- **Acceleration bar:** turn it on or off from the app. When enabled, the existing bar uses valid acceleration/speed data; it remains empty while stationary or without valid data.
- **Overspeed tolerance:** set **0–100 km/h**, default **20 km/h**, always entered in km/h even with mph selected. The red speed outline fades in from 75% of tolerance and is fully red at 100%; zero makes any positive overspeed fully red.
- All three settings are confirmed by BLE readback and saved in HUD NVS. Existing settings, phone owner and gateway bonds are retained. A BOOT phone-binding reset also retains these settings.


These controls need **app 1.4.1 + firmware v29.1.9.9**. App 1.4.1 still supports older firmware: missing controls are disabled. Maintainer-supplied APK 1.4.1 and v29.1.9.9 app-BIN are published in Releases; integrity was verified. If v29.0 and its OTA partition table are already installed, v29.1.9 uses the same layout; earlier firmware needs the initial USB/partition upgrade described below.


## HUD on hardware


![ESP32-S3 SuperMini HUD on the NV3007 2.79-inch display](docs/images/hud-supermini-nv3007.jpg)


Photo supplied by WARMW00D: speed, gear, speed-limit sign, navigation and fuel information on the NV3007 display.


## Download firmware BIN


[v29.1.9 release](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-ESP32-S3-SuperMini-LCD-2.79-NV3007-BLE/releases/tag/v29.1.9.9) · [App-BIN](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-ESP32-S3-SuperMini-LCD-2.79-NV3007-BLE/releases/download/v29.1.9.9/HUD-SuperMini-v29.1.9.9-app.bin) · [SHA-256](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-ESP32-S3-SuperMini-LCD-2.79-NV3007-BLE/releases/download/v29.1.9.9/SHA256SUMS.txt).


Maintainer-supplied image, **1,149,616 bytes**. ESP32-S3 header, image checksum and SHA-256 were verified. **OTA application only**: no bootloader or partition table. Initial installation requires USB with the project `partitions.csv`; this app-BIN cannot migrate partitions. The v29.0 two-slot OTA layout is compatible. Real-device OTA transfer has not been verified here.


## Download Android APK


[Version 1.3](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App/releases/tag/v1.4.1) — **Pre-release**, pending real-device OTA validation.


- [Release APK](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App/releases/download/v1.4.1/HUD-Control-1.4.1-release.apk) — normal installation, 58.4 KB.
- [Debug APK](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App/releases/download/v1.4.1/HUD-Control-1.4.1-debug.apk) — 66.5 KB; `testOnly=true`: `adb install -r -t HUD-Control-1.4.1-debug.apk`.
- [SHA-256](https://github.com/WARMW00D/HUD-Audi-A4-B9-MLB-Evo-Android-App/releases/download/v1.4.1/SHA256SUMS.txt).


WARMW00D supplied these builds: version 1.3 (versionCode 4). APK v2 signatures and content digests were verified. Each flavor retains its previous signing key: release updates release and debug updates debug. Switching flavors requires uninstalling the app and loses its local settings.


## What the HUD shows


| Element | Behaviour |
|---|---|
| Speed and gearbox | Cluster digital speed; P/R/N/D/S/M/E/Offroad and gear number when available |
| ACC and limiter | Set speed, ACC target and traffic jam assist; LIM replaces the ACC set-speed icon |
| Lane / Side Assist | Lane detection, steering and warnings; red lines for blind-spot indications |
