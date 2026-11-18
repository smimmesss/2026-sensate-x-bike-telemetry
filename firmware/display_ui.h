#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

/*
 * ST7789 1.14-inch TFT wiring supplied in the project specification.
 * The display is configured for a 240 x 135 logical canvas after rotation.
 */
namespace DisplayPins {
constexpr int8_t CS = 10;
constexpr int8_t DC = 8;
constexpr int8_t RST = 9;
constexpr int8_t MOSI = 11;
constexpr int8_t SCLK = 12;
constexpr int8_t BACKLIGHT = 13;
constexpr int8_t MISO = -1;  // Not connected by this display profile.
}

struct DisplayData {
  bool fixValid = false;
  double speedKmh = 0.0;
  double tripDistanceM = 0.0;
  double maxSpeedKmh = 0.0;
  double altitudeM = 0.0;
  double hdop = 0.0;
  uint32_t satellitesUsed = 0;
  uint8_t galileoSatellitesInView = 0;
  bool satellitesValid = false;
  bool galileoSatellitesValid = false;
  bool hdopValid = false;
  bool wifiConnected = false;
  bool firebaseReady = false;
  bool osnmaCommandSent = false;
  bool osnmaStatusReportSeen = false;
  const char *navigationInstruction = "No route set";
};

class DisplayUI {
 public:
  DisplayUI()
      : spi_(SPI),
        tft_(&spi_, DisplayPins::CS, DisplayPins::DC, DisplayPins::RST) {}

  void begin() {
    pinMode(DisplayPins::BACKLIGHT, OUTPUT);
    digitalWrite(DisplayPins::BACKLIGHT, HIGH);

    spi_.begin(DisplayPins::SCLK, DisplayPins::MISO,
               DisplayPins::MOSI, DisplayPins::CS);

    tft_.init(135, 240);
    tft_.setRotation(3);
    tft_.setTextWrap(false);
    tft_.fillScreen(ST77XX_BLACK);
    drawStaticLayout();
  }

  // Draw labels and separators once. Live fields are refreshed independently.
  void drawStaticLayout() {
    tft_.fillScreen(ST77XX_BLACK);
    tft_.setTextSize(1);
    tft_.setTextWrap(false);

    tft_.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft_.setCursor(4, 3);
    tft_.print(F("SMART BIKE COMPUTER"));
    tft_.drawFastHLine(4, 14, 232, ST77XX_BLUE);

    drawLabel(4, 23, F("SPD"));
    drawLabel(120, 23, F("TRIP"));
    drawLabel(4, 48, F("MAX"));
    drawLabel(120, 48, F("ALT"));
    drawLabel(4, 73, F("SAT"));
    drawLabel(120, 73, F("GAL"));
    drawLabel(4, 98, F("HDOP"));
    drawLabel(4, 116, F("NAV"));

    tft_.drawFastHLine(4, 111, 232, 0x4208);
    tft_.drawFastHLine(4, 132, 232, 0x4208);

    for (String &entry : lastFields_) {
      entry = "\x01";  // Force the first refresh.
    }
  }

  void update(const DisplayData &data) {
    const String speed = data.fixValid ? String(data.speedKmh, 1) + F(" km/h") : F("-- km/h");
    const String trip = data.tripDistanceM < 1000.0
                            ? String(data.tripDistanceM, 0) + F(" m")
                            : String(data.tripDistanceM / 1000.0, 2) + F(" km");
    const String maxSpeed = String(data.maxSpeedKmh, 1) + F(" km/h");
    const String altitude = data.fixValid ? String(data.altitudeM, 0) + F(" m") : F("-- m");
    const String sats = data.satellitesValid ? String(data.satellitesUsed) : F("--");
    const String gal = data.galileoSatellitesValid ? String(data.galileoSatellitesInView) : F("--");
    const String hdop = data.hdopValid ? String(data.hdop, 1) : F("--");

    writeField(0, 34, 22, 78, speed, ST77XX_WHITE);
    writeField(1, 158, 22, 78, trip, ST77XX_GREEN);
    writeField(2, 34, 47, 78, maxSpeed, ST77XX_WHITE);
    writeField(3, 158, 47, 78, altitude, ST77XX_WHITE);
    writeField(4, 34, 72, 78, sats, ST77XX_WHITE);
    writeField(5, 158, 72, 78, gal, ST77XX_WHITE);
    writeField(6, 42, 97, 85, hdop, ST77XX_WHITE);

    String nav = data.navigationInstruction ? String(data.navigationInstruction) : F("No route set");
    nav.trim();
    if (nav.length() > 31) nav = nav.substring(0, 31);
    writeField(7, 31, 115, 205, nav, ST77XX_ORANGE);

    String status = data.wifiConnected ? F("WIFI:ON") : F("WIFI:OFF");
    status += data.firebaseReady ? F("  CLOUD:ON") : F("  CLOUD:OFF");
    status += data.osnmaCommandSent ? F("  OSNMA:CMD") : F("  OSNMA:--");
    if (data.osnmaStatusReportSeen) status += F("*");
    writeField(8, 4, 124, 232, status, data.wifiConnected ? ST77XX_GREEN : ST77XX_RED);
  }

 private:
  SPIClass &spi_;
  Adafruit_ST7789 tft_;
  String lastFields_[9];

  void drawLabel(int16_t x, int16_t y, const __FlashStringHelper *label) {
    tft_.setCursor(x, y);
    tft_.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft_.print(label);
  }

  void writeField(uint8_t index, int16_t x, int16_t y, int16_t width,
                  const String &value, uint16_t color) {
    if (index >= (sizeof(lastFields_) / sizeof(lastFields_[0]))) return;
    if (lastFields_[index] == value) return;

    tft_.fillRect(x, y - 1, width, 11, ST77XX_BLACK);
    tft_.setCursor(x, y);
    tft_.setTextSize(1);
    tft_.setTextColor(color, ST77XX_BLACK);
    tft_.print(value);
    lastFields_[index] = value;
  }
};
