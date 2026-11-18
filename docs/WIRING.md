# Hardware Wiring & ESD Guide

This document records the wiring profile used by the Smart Bike Computer project. It combines the supplied project pin map with the BIP26 SENSATE-X 2.0 lab guide. Confirm carrier-board silkscreen labels and electrical levels before applying power.

## 1. System wiring overview

```text
             USB-C
               |
       +------------------+
       | Tenstar ESP32-S3  |
       |                  |
       | GPIO17  UART RX  |<------ LC29H TXD0
       | GPIO18  UART TX  |------> LC29H RXD0
       | GND              |------- LC29H GND
       |                  |
       | GPIO11 MOSI      |------> ST7789 SDA / MOSI
       | GPIO12 SCLK      |------> ST7789 SCL / SCLK
       | GPIO10 CS        |------> ST7789 CS
       | GPIO8  DC        |------> ST7789 DC
       | GPIO9  RST       |------> ST7789 RST
       | GPIO13 BL        |------> ST7789 BL / backlight control
       | 3V3 / GND        |------> TFT power (if supported by display carrier)
       +------------------+

       LC29H(AA) IPX/U.FL socket <---- external active GNSS antenna
```

The GNSS reference wiring is a three-wire UART connection: common ground, LC29H TX to ESP32-S3 RX, and ESP32-S3 TX to LC29H RX. The BIP26 Student Getting Started Guide identifies Tenstar A1 as GPIO17/RX and A0 as GPIO18/TX (page 5).

## 2. GNSS UART connections

| Cable (BIP26 guide) | Waveshare LC29H carrier | Tenstar TS-ESP32-S3 | Signal direction | Notes |
|---|---|---|---|---|
| Black | Pin 6 — GND | GND | Common | Establish a common reference before UART communication. |
| Yellow | Pin 8 — Pi TXD0 (LC29H transmits) | A1 / GPIO17 (UART RX) | LC29H TX → ESP32 RX | Receiver NMEA output. |
| Blue | Pin 10 — Pi RXD0 (LC29H receives) | A0 / GPIO18 (UART TX) | ESP32 TX → LC29H RX | Sends Quectel PQTM configuration commands. |

Firmware serial configuration:

- UART: `Serial1` / ESP32-S3 UART1.
- Baud: `115200`.
- Format: `8 data bits, no parity, 1 stop bit` (`SERIAL_8N1`).
- ESP32-S3 RX: GPIO17.
- ESP32-S3 TX: GPIO18.

**TX/RX rule:** connect TX on one device to RX on the other device. Do not connect TX-to-TX or RX-to-RX. If the Serial Monitor shows no NMEA characters, first check common ground, then verify the TX/RX orientation and the configured baud rate.

### Electrical-level caution

The Quectel LC29H module specification lists a typical I/O voltage of 2.8 V and a module supply range of 3.1–3.6 V. The Waveshare carrier may add regulation or level translation, but this depends on the exact board revision. Do not assume that a bare LC29H module pin is 3.3 V tolerant. Check the carrier schematic/manual. If its UART interface is not explicitly 3.3 V compatible, add a suitable level shifter on the ESP32-S3 TX → LC29H RX path before connecting it. Never apply 5 V directly to a bare LC29H module I/O or supply pin.

The BIP26 tutorial documents the three-wire arrangement above but does not specify a separate VCC jumper in that table. Therefore, do not infer a power pin from this document: power the receiver carrier only through the supply input identified for your exact Waveshare board. Do not connect an unverified 5 V or 3.3 V pin.

## 3. TFT ST7789 SPI connections

| ST7789 signal | ESP32-S3 GPIO | Direction | Purpose |
|---|---:|---|---|
| CS | GPIO10 | ESP32 → TFT | SPI chip select |
| DC | GPIO8 | ESP32 → TFT | Data / command selection |
| RST | GPIO9 | ESP32 → TFT | Display reset |
| MOSI / SDA | GPIO11 | ESP32 → TFT | SPI display data |
| SCLK / SCL | GPIO12 | ESP32 → TFT | SPI clock |
| BL | GPIO13 | ESP32 → TFT/carrier | Backlight enable/control |
| MISO | Not connected | — | Not needed for write-only display updates |
| VCC | Board 3V3 only if carrier accepts 3.3 V | Supply | Verify the display carrier label/datasheet first |
| GND | ESP32-S3 GND | Common | Common reference |

The firmware uses the requested project SPI pin profile and initializes the ST7789 as a 135 × 240 panel, rotated to a 240 × 135 landscape canvas.

**Backlight caution:** GPIO13 should drive only a backlight-enable input or a suitable transistor/driver stage. Do not source the TFT LED backlight current directly from an ESP32 GPIO unless the display carrier documentation confirms that its BL input is logic-level and within the pin current limits. If the board has a current-limited backlight input, follow its vendor wiring specification.

## 4. External GNSS antenna (IPX / U.FL)

1. Place the LC29H carrier on the anti-static mat and disconnect power before handling the RF connector.
2. Align the antenna's IPX/U.FL plug directly above the module socket.
3. Press vertically until seated. Do not twist, rock, or angle the plug.
4. Route the antenna cable without sharp bends and position the antenna near a window or with a clear view of the sky.
5. To remove it, lift the connector vertically with an appropriate connector tool; never pull the coaxial cable sideways.

The BIP26 getting-started guide (pages 4–5) notes that walls, ceilings, and metal objects attenuate GNSS signals, and that initial acquisition can take approximately 1–2 minutes. The guide also describes the LC29H PPS LED flashing once per second when satellites are locked.

## 5. Power-up and first test

1. With all boards unpowered, inspect the wiring and confirm the common ground.
2. Attach the GNSS antenna before placing the receiver under power.
3. Connect the Tenstar board to the development PC using USB-C.
4. Upload the sketch using the board settings listed in the repository README.
5. Press the Tenstar RST button after upload if the sketch does not start automatically (BIP26 Getting Started Guide, page 7).
6. Open Serial Monitor at `115200` baud. Check for NMEA output and the OSNMA command/status diagnostics.
7. Test GNSS outdoors or near a window. A valid PVT fix is not guaranteed indoors.

## 6. ESD / anti-static precautions

The Tenstar board, GNSS receiver, and TFT assembly contain ESD-sensitive electronics. Follow the BIP26 lab guidance (Getting Started Guide, page 7):

- Work on a grounded anti-static mat; touch the mat before picking up a board.
- Use an ESD wrist strap connected to the mat/approved ground point where available.
- Hold PCBs by their edges. Avoid touching exposed pins, IC leads, antenna contacts, or the TFT active surface.
- Do not slide boards across ordinary desks, clothing, or plastic packaging.
- Keep boards in anti-static bags when not installed.
- Disconnect USB and all external power before changing jumpers, moving the TFT, or attaching/removing the IPX antenna.
- Do not hot-plug UART wires or the display ribbon/jumpers while the boards are powered.
- Avoid conductive tools and loose screws near powered electronics.

ESD damage may occur without a visible spark and can permanently damage the display or receiver. If the display is dark, power down before inspecting the backlight and wiring; do not repeatedly hot-plug it.

## 7. Important pin-map reconciliation

The uploaded `BikeComputer.ino` used a different TFT map (CS 7, DC 39, RST 40, BL 45, SCLK 36, MOSI 35, MISO 37). This refactored repository uses the pin map stated in the project specification instead: CS 10, DC 8, RST 9, BL 13, SCLK 12, MOSI 11, with no MISO connection. Do not mix the two profiles. Confirm the actual Tenstar header labels and board revision before wiring.

## 8. Reference documents

- BIP26 SENSATE-X 2.0, *Student Getting Started Guide*, sections 3–6 (antenna, UART wiring, upload, ESD).
- BIP26 SENSATE-X 2.0, *NMEA Sentences Tutorial*, sections 2, 4, 5 and 6 (NMEA sentence structure, GGA/RMC/GSV, checksums, Quectel PQTM commands).
- Quectel, *LC29H Series GNSS Module Specification*, version 2.0 (supply and I/O voltage, supported bands, interfaces and OSNMA feature list).
