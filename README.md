# Smart Bike Computer & IoT Activity Tracker

[![ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3-blue)](https://www.espressif.com/en/products/socs/esp32-s3)
[![C++](https://img.shields.io/badge/Language-C%2B%2B-00599C)](https://isocpp.org/)
[![Firebase](https://img.shields.io/badge/Cloud-Firebase-FFCA28)](https://firebase.google.com/)
[![Galileo](https://img.shields.io/badge/GNSS-Galileo%20%7C%20OSNMA-003399)](https://www.euspa.europa.eu/european-space/galileo)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A standalone bicycle computer and IoT activity tracker developed during the **BIP26 SENSATE-X 2.0 programme at TU Dublin**. The embedded unit receives GNSS navigation data, computes trip metrics, renders them on a compact ST7789 TFT and synchronises telemetry with Firebase Realtime Database over Wi-Fi.


## System architecture

```text
    GNSS antenna (IPX/U.FL)
              |
              v
    Waveshare / Quectel LC29H(AA)
    - Galileo E1 / E5a + other GNSS
    - NMEA 0183 at 115200 baud
    - OSNMA-capable receiver
              |
        UART1: TX/RX
        GPIO17 / GPIO18
              |
              v
    Tenstar TS-ESP32-S3 (FH4R2)
    +------------------------------------+
    | NMEA parser (TinyGPSPlus)          |
    | GAGSV checksum / Galileo SV count |
    | Trip odometer + quality filter     |
    | OSNMA PQTM command/status logging  |
    |                                    |
    | SPI profile                        |----> ST7789 TFT
    | Wi-Fi + TLS Firebase client        |
    +------------------------------------+
              |
          Wi-Fi / HTTPS
              |
              v
    Firebase Realtime Database
      |                   |
      | telemetry / track | navigation instruction
      v                   v
    Web dashboard      Route guidance source
      |
      +---- Map (Leaflet / OpenStreetMap tiles)
      +---- Daily activity summaries
      +---- Geofence boundary evaluation
      +---- Safety alert / parent notification service
```

## Key capabilities

- **PVT telemetry:** position, speed, course, altitude, date/time, satellites used and HDOP are parsed from NMEA 0183 using TinyGPSPlus. The firmware applies a basic HDOP/speed/distance sanity filter to trip distance.
- **Galileo visibility:** the firmware reads checksum-valid `$GAGSV` sentences to expose Galileo satellites in view. `$GNGGA` and related combined talker sentences can still represent multi-constellation fixes; total satellites used must not be interpreted as Galileo-only.
- **OSNMA configuration:** at startup the ESP32 sends the LC29H proprietary command `$PQTMCFGosnma,W,1`. A `$PQTMOSNMA` status sentence is detected and logged when present. This project code does not decode the vendor-specific status payload into a cryptographically verified `authenticated` result; command transmission or receipt of a status sentence alone is not proof that a particular fix passed authentication.
- **Firebase synchronisation:** the latest telemetry is written to `/telemetrie_bici`; accepted route points are appended to `/telemetrie_bici/track`; navigation text is read from `/nav_instruction`.
- **Flicker-conscious TFT updates:** the static layout is drawn once. Individual value regions are cleared and redrawn only when the displayed string changes.
- **Geofencing integration contract:** the location stream can be consumed by the web client or a trusted backend to compare a rider's position with a configured circular/polygonal boundary and raise an alert when an enter/exit event occurs. Geofence evaluation and push notifications are not implemented in the supplied firmware files; do not treat the prototype as an emergency or child-safety device.

## Hardware bill of materials

| Item | Model / description | Purpose |
|---|---|---|
| Main controller | Tenstar TS-ESP32-S3, ESP32-S3 FH4R2 | GNSS parsing, local UI, Wi-Fi and Firebase communications |
| GNSS receiver | Waveshare carrier with Quectel LC29H(AA) dual-band GNSS | NMEA PVT output and Galileo OSNMA-capable receiver |
| GNSS antenna | External active/passive antenna with IPX/U.FL plug compatible with carrier | Satellite reception; select a supported antenna and supply arrangement for the carrier |
| Display | 1.14-inch ST7789 TFT, 135 × 240 | Local speed, trip and GNSS status |
| Interconnect | Dupont jumper wires | UART and TFT signal connections |
| Power/programming | USB-C cable and regulated supply/power bank compatible with Tenstar board | Development, upload and device power |
| ESD accessories | Anti-static mat and ESD wrist strap | Safe handling during assembly |

Check the exact carrier-board revision and power requirements before powering the GNSS and TFT modules. See [docs/WIRING.md](docs/WIRING.md) for electrical-level and ESD cautions.

## Pinout

### GNSS UART

| LC29H carrier | Tenstar board | ESP32-S3 GPIO | Direction |
|---|---|---:|---|
| Pin 6 — GND | GND | GND | Common ground |
| Pin 8 — Pi TXD0 (receiver TX) | A1 | 17 (RX) | LC29H TX → ESP32 RX |
| Pin 10 — Pi RXD0 (receiver RX) | A0 | 18 (TX) | ESP32 TX → LC29H RX |

UART configuration: `115200`, `SERIAL_8N1`. The A1/A0 labels and GPIO numbers are taken from the BIP26 Student Getting Started Guide (page 5).

### TFT SPI profile from the project specification

| TFT signal | ESP32-S3 GPIO |
|---|---:|
| CS | 10 |
| DC | 8 |
| RST | 9 |
| MOSI / SDA | 11 |
| SCLK / SCL | 12 |
| BL | 13 |
| MISO | Not connected |

**Pin-map note:** the uploaded legacy `BikeComputer.ino` contains an alternative TFT profile (CS=7, DC=39, RST=40, BL=45, MOSI=35, SCLK=36, MISO=37). The refactored source deliberately follows the project brief's profile above. Do not combine these two maps.

## Hardware and protocol notes

### NMEA and constellation interpretation

NMEA 0183 messages are plain-text sentences with a talker ID, sentence type, comma-separated fields and an XOR checksum. The BIP26 NMEA tutorial identifies `GN` as combined multi-constellation data and `GA` as Galileo-specific. The firmware uses TinyGPSPlus for general PVT fields and a small checksum-checked `$GAGSV` handler for Galileo satellites-in-view. TinyGPSPlus's total satellite count is the receiver's overall count used in the fix, not a Galileo-only count.

### OSNMA scope

Quectel lists the LC29H(AA) as an OSNMA-capable standard-precision variant. The firmware sends the PQTM enable command and reports whether a `$PQTMOSNMA` line has been observed. It intentionally avoids interpreting undocumented PQTM status fields. For a safety- or security-sensitive deployment, follow the current Quectel LC29H(AA) OSNMA Application Note, confirm the receiver firmware version and trust-anchor provisioning, and validate the receiver's authentication result using the vendor's documented status format.

### HAS limitation — do not claim HAS verification

The Quectel LC29H Series Specification v2.0 lists Galileo reception on **E1 and E5a** for LC29H(AA). It does not list E6 reception for this variant. Galileo HAS correction data is broadcast on E6. Therefore this LC29H(AA)-based project must not claim that it receives, applies or verifies Galileo HAS corrections. A separate E6/HAS-capable receiver and a validated correction workflow would be required.

## Software prerequisites

- Arduino IDE 2.x.
- Espressif ESP32 Arduino board package.
- USB data cable for the Tenstar board.
- A Firebase project with Realtime Database enabled and Email/Password Authentication configured.
- The libraries below (install dependencies when Library Manager prompts you).

### Arduino board package settings

In Arduino IDE, add this Boards Manager URL under **File → Preferences → Additional boards manager URLs**:

```text
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Install **esp32 by Espressif Systems** (not the similarly named Arduino package), then select **ESP32S3 Dev Module**. The BIP26 lab guide specifies these board options:

| Tools menu setting | BIP26 value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 4MB (32Mb) |
| Upload Mode | UART0 / Hardware CDC |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |
| PSRAM | Disabled |

The correct flash/PSRAM settings can vary with a board revision. Confirm them against the exact Tenstar unit if your board has a different memory configuration.

### Arduino libraries

Install through **Sketch → Include Library → Manage Libraries**:

| Library | Used for |
|---|---|
| TinyGPSPlus | NMEA sentence parsing |
| Adafruit ST7735 and ST7789 Library | ST7789 controller driver |
| Adafruit GFX Library | TFT graphics primitives (dependency of the display library) |
| FirebaseClient (Mobizt) | Firebase Realtime Database and user authentication |
| ESP_SSLClient | TLS client dependency used by FirebaseClient; normally installed as a dependency |

`WiFi.h`, `SPI.h`, `time.h` and `HardwareSerial` are provided by the ESP32 Arduino core.

> **Firebase library selection:** the original project brief names `Firebase-ESP-Client`. Its maintainer has marked that library deprecated/end-of-life and recommends `FirebaseClient` for ongoing support. This refactor therefore uses `FirebaseClient` rather than introducing a known-deprecated dependency. See the project links below.

## Repository layout

```text
smart-bike-computer-iot/
├── README.md
├── LICENSE
├── .gitignore
├── firmware/
│   ├── smart_bike_main.ino
│   ├── display_ui.h
│   └── config.h.example
└── docs/
    └── WIRING.md
```

## Installation and setup

### 1. Clone the repository

```bash
git clone https://github.com/<your-account>/smart-bike-computer-iot.git
cd smart-bike-computer-iot
```

### 2. Create the private configuration header

Copy the example and edit the credentials:

```bash
cp firmware/config.h.example firmware/config.h
```

On Windows, copy the file in Explorer or use:

```powershell
Copy-Item firmware/config.h.example firmware/config.h
```

Fill in `WIFI_SSID`, `WIFI_PASSWORD`, Firebase API key, a dedicated Firebase device user's email/password and the Realtime Database root URL. Add the PEM root CA certificate used to validate the TLS certificate chain for your actual database hostname. The firmware will deliberately leave Firebase disabled when credentials or the trusted CA are missing; it does not use `setInsecure()` as a fallback.

### 3. Configure Firebase

1. Create or open a project in [Firebase Console](https://console.firebase.google.com/).
2. Enable **Authentication → Sign-in method → Email/Password**.
3. Add a dedicated device account under **Authentication → Users**. Do not reuse a personal account.
4. Create a **Realtime Database** and copy its root URL from the Data tab. Keep the URL root-only, without `/telemetrie_bici` or `.json`.
5. Copy the Web API key from **Project settings → General**.
6. Set the database security rules to require authentication. The following is a starter rule set for the legacy top-level paths used by this firmware; it is not a complete multi-tenant authorization design:

```json
{
  "rules": {
    "telemetrie_bici": {
      ".read": "auth != null",
      ".write": "auth != null"
    },
    "nav_instruction": {
      ".read": "auth != null",
      ".write": "auth != null"
    }
  }
}
```

For deployment with multiple riders, migrate to a user/device-scoped path (for example `/users/{uid}/telemetry`) and write rules that bind each path to the authenticated UID. Never leave the database in public test mode.

The existing data paths are:

| Path | Access pattern | Purpose |
|---|---|---|
| `/telemetrie_bici` | `update` / PATCH | Latest telemetry snapshot; preserves the route-history child |
| `/telemetrie_bici/track` | `push` / append | Timestamped route points |
| `/nav_instruction` | `get` / read | Text instruction shown on TFT |

Example telemetry record:

```json
{
  "latitude": 53.33539,
  "longitude": -6.25927,
  "speedKmh": 18.4,
  "courseDeg": 92.0,
  "altitudeM": 85.4,
  "tripDistanceM": 1240.5,
  "maxSpeedKmh": 31.2,
  "satellitesUsed": 11,
  "hdop": 0.9,
  "galileoSatellitesInView": 7,
  "galileoGsvValid": true,
  "osnmaCommandSent": true,
  "osnmaStatusReportSeen": false
}
```

The values above are illustrative only, not measured project data.

### 4. Configure and upload firmware

1. Open `firmware/smart_bike_main.ino` in Arduino IDE.
2. Ensure `config.h` is in the same `firmware/` directory as the sketch and `display_ui.h`.
3. Select the Tenstar ESP32-S3 serial port and the board settings above.
4. Compile and upload.
5. Press the Tenstar **RST** button after upload if the program does not start automatically. If upload cannot connect, use the BIP26 boot-mode sequence: hold BOOT, press and release RST, release BOOT, then retry upload.
6. Open Serial Monitor at `115200` baud.

Arduino IDE versions can enforce a sketch-folder naming convention. If the IDE asks to move/rename the sketch, place the `.ino`, `display_ui.h` and local `config.h` together in a folder named `smart_bike_main` and open `smart_bike_main.ino` from there.

### 5. Verify the system

- Check Serial Monitor for NMEA input and a valid location fix.
- Wait for open-sky acquisition; the BIP26 lab guide notes that initial acquisition may take 1–2 minutes.
- Check that speed, trip, altitude, satellites and HDOP refresh on the TFT.
- Confirm `$GAGSV` output is enabled by the receiver if you expect the Galileo-only satellites-in-view field to populate.
- Check the receiver's PQTM output and Quectel documentation to verify OSNMA status. The UI indicator means the command was sent / a status sentence was seen; it is not an authentication verdict.
- Confirm the `telemetrie_bici` node updates in Firebase and that track points appear under `telemetrie_bici/track`.
- Set `nav_instruction` to a string value in Firebase and confirm it appears on the TFT.

## Dashboard, route and geofencing integration

The web component described by the project architecture can use Firebase SDK + Leaflet to subscribe to `/telemetrie_bici`, draw the appended `/track` points, and render an activity summary. A geofence service should store each rider's allowed boundary separately from telemetry, evaluate point-in-polygon or distance-to-centre on each fresh location, apply a configurable dwell/hysteresis period to avoid GPS-noise alerts, and send notifications through a trusted backend (for example, Firebase Cloud Functions + an approved messaging provider).

A suggested future data model is:

```text
/geofences/{uid}/{fenceId}
    enabled: true
    name: "Home area"
    center: { latitude: 53.33, longitude: -6.25 }
    radiusM: 250
    alertOnExit: true
    alertOnEnter: false

/geofenceEvents/{uid}/{eventId}
    type: "exit"
    recordedAt: <server timestamp>
    latitude: <latest latitude>
    longitude: <latest longitude>
```

Do not send parent/guardian safety alerts directly from an unauthenticated browser. Enforce per-user access rules and generate notifications from a trusted server-side component. GNSS errors, network loss and a depleted battery can all delay or prevent an alert; this prototype must not be the only safety control.

## Pre-deployment validation checklist

The repository is source-complete for the requested firmware files, but it has not been compiled against your exact board package or exercised on the physical hardware in this workspace. Complete these checks before using it on a bicycle or relying on it for safety-related behaviour:

- [ ] Compile with the selected Espressif ESP32 Arduino core and installed library versions.
- [ ] Confirm the Tenstar board's exact GPIO breakout and the requested ST7789 wiring profile; the legacy sketch used a different map.
- [ ] Verify UART voltage compatibility on the exact Waveshare carrier and add level translation if its RX input is not 3.3 V tolerant.
- [ ] Confirm that `$PQTMCFGosnma,W,1` is accepted by the installed LC29H(AA) firmware and interpret its response only using the current Quectel application note.
- [ ] Validate the Firebase CA chain and authentication against the actual database hostname; keep `setInsecure()` disabled.
- [ ] Test GNSS drift rejection, track point cadence, Firebase reconnect behaviour and navigation fetches over a long ride.
- [ ] Test dashboard geofence hysteresis, notification delivery, offline handling and user-scoped database rules independently.
- [ ] Verify battery runtime, enclosure/weather protection, vibration resistance and safe mounting.

## Accuracy and security caveats

- GNSS accuracy depends on antenna placement, sky visibility, multipath, constellation geometry and receiver settings. HDOP is a geometry indicator, not a guaranteed metre error.
- The odometer is a GNSS-derived estimate; it is not a wheel encoder and is not calibrated for certified distance measurement.
- OSNMA authenticates Galileo navigation messages, not every RF signal and not the whole application. The supplied firmware does not independently perform cryptographic verification.
- LC29H(AA) has no E6/HAS reception listed in its specification; HAS is not implemented here.
- Firebase security rules and TLS trust anchors are part of the security boundary. Never commit `config.h`, private keys or user passwords.

## Troubleshooting

| Symptom | Checks |
|---|---|
| No NMEA characters | Check GND, TX↔RX direction, GPIO17/18, 115200 baud and receiver power. |
| NMEA arrives but no fix | Move antenna to open sky, check IPX seating and wait for acquisition. |
| Galileo count shows `--` | Confirm the receiver outputs `$GAGSV` sentences with valid checksums. |
| TFT remains dark | Check the requested SPI pin profile, power/ground, BL control and board ESD condition. |
| Firebase remains offline | Verify Wi-Fi, NTP time, API key, Email/Password device account, database URL, RTDB rules and PEM CA chain. |
| Navigation text is empty | Confirm `/nav_instruction` contains a string value and the authenticated user can read it. |

## License

Distributed under the MIT License; see [LICENSE](LICENSE). Confirm that this choice is compatible with any TU Dublin course or programme intellectual-property requirements before public release.

## Acknowledgements

Developed in the context of **BIP26 SENSATE-X 2.0, TU Dublin**. Thanks to the programme and lab materials for the Tenstar TS-ESP32-S3, Waveshare LC29H(AA) GNSS hardware, Arduino setup guidance, NMEA tutorial and Galileo GNSS tutorial. Hardware specifications and upstream software documentation remain the authoritative references for component behaviour.

## References

- BIP26 SENSATE-X 2.0 — *Student Getting Started Guide* (TU Dublin; supplied project reference, especially the board setup, wiring, antenna and ESD sections).
- [Quectel LC29H Series product page](https://www.quectel.com/product/gnss-lc29h/).
- [Quectel LC29H Series GNSS Module Specification v2.0 (PDF)](https://www.quectel.com/content/uploads/2026/03/Quectel_LC29H_Series_GNSS_Module_Specification_V2.0.pdf).
- Quectel LC29H(AA) OSNMA Application Note (listed in the [LC29H product documentation](https://www.quectel.com/product/gnss-lc29h/); consult the current vendor release for exact status fields and configuration behaviour).
- BIP26 SENSATE-X 2.0 — *Galileo GNSS Tutorial* (TU Dublin; supplied project reference).
- BIP26 SENSATE-X 2.0 — *NMEA Sentences Tutorial* (TU Dublin; supplied project reference).
- [FirebaseClient library (Mobizt)](https://github.com/mobizt/FirebaseClient).
- [Deprecated Firebase-ESP-Client notice](https://github.com/mobizt/Firebase-ESP-Client).
- [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus).
- [Adafruit ST7735/ST7789 library](https://github.com/adafruit/Adafruit-ST7735-Library) and [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library).
