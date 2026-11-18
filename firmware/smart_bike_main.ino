/*
 * Smart Bike Computer & IoT Activity Tracker
 * BIP26 SENSATE-X 2.0 | TU Dublin
 *
 * Target: Tenstar TS-ESP32-S3 (ESP32-S3 FH4R2)
 * GNSS:   Waveshare / Quectel LC29H(AA), UART1, 115200 baud
 * TFT:    1.14-inch ST7789, custom FSPI pin profile in display_ui.h
 * Cloud:  Firebase Realtime Database using the maintained FirebaseClient API
 *
 * IMPORTANT IMPLEMENTATION NOTES
 * - Copy config.h.example to config.h and configure credentials + TLS CA.
 * - This sketch reads/parses NMEA in the main loop; it deliberately avoids a
 *   second FreeRTOS task sharing the TinyGPSPlus parser instance.
 * - Bench-compile and hardware-validate the exact board/libraries/pin levels
 *   before riding; this source has not been exercised on the physical unit.
 * - The OSNMA command is sent to the receiver. A proprietary PQTM status line
 *   is observed/logged, but its undocumented payload is NOT interpreted as a
 *   cryptographic authentication verdict by this firmware.
 * - LC29H(AA) datasheet lists Galileo E1/E5a, not E6. Galileo HAS is therefore
 *   not claimed or validated by this hardware/software combination.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <TinyGPS++.h>

#include "config.h"

// FirebaseClient v2.x compile-time feature switches must precede its header.
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#include <FirebaseClient.h>
#include <ESP_SSLClient.h>

#include "display_ui.h"

namespace Timing {
constexpr uint32_t DISPLAY_INTERVAL_MS = 250;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
constexpr uint32_t TRACK_POINT_INTERVAL_MS = 5000;
constexpr uint32_t NAVIGATION_FETCH_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t GNSS_FIX_MAX_AGE_MS = 5000;
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 15000;
}

namespace TripFilter {
constexpr double MIN_SPEED_KMH = 2.5;
constexpr double MAX_SEGMENT_METERS = 50.0;
constexpr double MIN_SEGMENT_METERS = 0.5;
constexpr double MAX_HDOP = 2.0;
}

// GNSS parser and serial transport.
HardwareSerial GNSSSerial(1);
TinyGPSPlus gps;
DisplayUI displayUI;

// FirebaseClient objects. The CA certificate is loaded only when configured.
ESP_SSLClient firebaseSslClient;
AsyncClientClass firebaseAsyncClient(firebaseSslClient);
UserAuth firebaseUserAuth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL,
                          FIREBASE_USER_PASSWORD, 3000);
FirebaseApp firebaseApp;
RealtimeDatabase firebaseDatabase;

// Runtime state.
String navigationInstruction = "No route set";
char nmeaLine[192] = {0};
size_t nmeaLineLength = 0;

bool osnmaCommandSent = false;
bool osnmaStatusReportSeen = false;
bool firebaseStarted = false;
bool firebaseStartAttempted = false;
bool ntpRequested = false;
bool trackPointPending = false;
bool havePreviousPoint = false;

uint8_t galileoSatellitesInView = 0;
bool galileoSatellitesValid = false;

double previousLatitude = 0.0;
double previousLongitude = 0.0;
double totalDistanceMeters = 0.0;
double maximumSpeedKmh = 0.0;

uint32_t lastDisplayUpdate = 0;
uint32_t lastTelemetryUpload = 0;
uint32_t lastTrackUpload = 0;
uint32_t lastNavigationFetch = 0;
uint32_t lastWifiAttempt = 0;
uint32_t lastNtpAttempt = 0;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void readGnssStream();
void processRawNmeaLine(char *line);
void processOdometer();
void maintainWiFi();
void initialiseFirebaseWhenReady();
void uploadTelemetry(bool includeTrackPoint);
void fetchNavigationInstruction();
void firebaseCallback(AsyncResult &result);
bool isFixFresh();
bool nmeaChecksumValid(const char *line);
bool extractGalileoSatellites(char *line, uint8_t &satelliteCount);
void sendOsnmaEnableCommand();
void updateDisplay();

// -----------------------------------------------------------------------------
// Setup and main scheduler
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F(" Smart Bike Computer | BIP26 SENSATE-X 2.0"));
  Serial.println(F("=============================================="));

  displayUI.begin();

  GNSSSerial.setRxBufferSize(4096);
  GNSSSerial.begin(GNSS_BAUD_RATE, SERIAL_8N1, GNSS_RX_GPIO, GNSS_TX_GPIO);
  Serial.printf("GNSS UART started: RX GPIO %d, TX GPIO %d, %lu baud\n",
                GNSS_RX_GPIO, GNSS_TX_GPIO,
                static_cast<unsigned long>(GNSS_BAUD_RATE));

  delay(500);  // Allow the receiver UART to become ready after ESP32 startup.
  sendOsnmaEnableCommand();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.println(F("Wi-Fi connection requested (non-blocking)."));

  updateDisplay();
}

void loop() {
  // Drain UART first on every pass so network work cannot starve NMEA parsing.
  readGnssStream();
  processOdometer();

  maintainWiFi();
  initialiseFirebaseWhenReady();

  if (firebaseStarted) {
    firebaseApp.loop();
  }

  const uint32_t now = millis();

  if (firebaseStarted && firebaseApp.ready()) {
    if (now - lastTelemetryUpload >= Timing::TELEMETRY_INTERVAL_MS) {
      lastTelemetryUpload = now;
      uploadTelemetry(false);
    }

    if (now - lastTrackUpload >= Timing::TRACK_POINT_INTERVAL_MS && trackPointPending) {
      lastTrackUpload = now;
      uploadTelemetry(true);
    }

    if (now - lastNavigationFetch >= Timing::NAVIGATION_FETCH_INTERVAL_MS) {
      lastNavigationFetch = now;
      fetchNavigationInstruction();
    }
  }

  if (now - lastDisplayUpdate >= Timing::DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // Yield briefly without blocking the UART for long periods.
  delay(1);
}

// -----------------------------------------------------------------------------
// GNSS stream handling
// -----------------------------------------------------------------------------
void readGnssStream() {
  while (GNSSSerial.available() > 0) {
    const char incoming = static_cast<char>(GNSSSerial.read());
    gps.encode(incoming);  // TinyGPSPlus validates standard NMEA checksums.

    if (incoming == '\n') {
      nmeaLine[nmeaLineLength] = '\0';
      if (nmeaLineLength > 0) processRawNmeaLine(nmeaLine);
      nmeaLineLength = 0;
    } else if (incoming != '\r') {
      if (nmeaLineLength < sizeof(nmeaLine) - 1) {
        nmeaLine[nmeaLineLength++] = incoming;
      } else {
        // Discard an overlong line and resynchronise at the next newline.
        nmeaLineLength = 0;
      }
    }
  }
}

void processRawNmeaLine(char *line) {
  if (line == nullptr || line[0] != '$') return;
  if (!nmeaChecksumValid(line)) return;

  // The BIP26 NMEA guide identifies $GAGSV as Galileo-specific satellites-in-view.
  if (strncmp(line, "$GAGSV,", 7) == 0) {
    char workingCopy[sizeof(nmeaLine)];
    strlcpy(workingCopy, line, sizeof(workingCopy));
    uint8_t count = 0;
    if (extractGalileoSatellites(workingCopy, count)) {
      galileoSatellitesInView = count;
      galileoSatellitesValid = true;
    }
    return;
  }

  // Preserve the raw proprietary status line for diagnostics. The LC29H PQTM
  // payload is vendor-defined; do not infer authenticated/failed from its text.
  if (strncmp(line, "$PQTMOSNMA", 10) == 0) {
    osnmaStatusReportSeen = true;
    Serial.print(F("LC29H OSNMA status sentence: "));
    Serial.println(line);
  }
}

bool nmeaChecksumValid(const char *line) {
  if (line == nullptr || line[0] != '$') return false;
  const char *star = strchr(line, '*');
  if (star == nullptr || strlen(star + 1) < 2) return false;

  uint8_t checksum = 0;
  for (const char *cursor = line + 1; cursor < star; ++cursor) {
    checksum ^= static_cast<uint8_t>(*cursor);
  }

  char expectedText[3] = {star[1], star[2], '\0'};
  char *endPointer = nullptr;
  const long expected = strtol(expectedText, &endPointer, 16);
  return endPointer != expectedText && expected >= 0 && expected <= 255 &&
         checksum == static_cast<uint8_t>(expected);
}

bool extractGalileoSatellites(char *line, uint8_t &satelliteCount) {
  // GSV fields: $GAGSV,total-messages,message-number,total-satellites,...
  char *checksumStart = strchr(line, '*');
  if (checksumStart != nullptr) *checksumStart = '\0';

  char *savePointer = nullptr;
  char *token = strtok_r(line, ",", &savePointer);
  uint8_t fieldIndex = 0;
  while (token != nullptr) {
    if (fieldIndex == 3) {
      const int parsed = atoi(token);
      if (parsed < 0 || parsed > 99) return false;
      satelliteCount = static_cast<uint8_t>(parsed);
      return true;
    }
    ++fieldIndex;
    token = strtok_r(nullptr, ",", &savePointer);
  }
  return false;
}

void sendOsnmaEnableCommand() {
#if GNSS_ENABLE_OSNMA
  // Command spelling is taken from the BIP26 NMEA tutorial / Quectel PQTM
  // command family. The receiver's own PQTM response should be checked during
  // commissioning; command transmission alone is not proof of authentication.
  GNSSSerial.print(F("$PQTMCFGosnma,W,1\r\n"));
  osnmaCommandSent = true;
  Serial.println(F("OSNMA enable command sent to LC29H."));
#else
  Serial.println(F("OSNMA auto-enable disabled at build time."));
#endif
}

// -----------------------------------------------------------------------------
// Trip metrics and fix quality
// -----------------------------------------------------------------------------
bool isFixFresh() {
  return gps.location.isValid() &&
         gps.location.age() <= Timing::GNSS_FIX_MAX_AGE_MS;
}

void processOdometer() {
  if (!gps.location.isUpdated() || !gps.location.isValid()) return;

  const double latitude = gps.location.lat();
  const double longitude = gps.location.lng();
  const bool hdopGood = gps.hdop.isValid() &&
                        gps.hdop.hdop() <= TripFilter::MAX_HDOP;

  if (!hdopGood) return;
  trackPointPending = true;

  if (gps.speed.isValid()) {
    const double speedKmh = gps.speed.kmph();
    if (speedKmh > maximumSpeedKmh) maximumSpeedKmh = speedKmh;
  }

  if (!havePreviousPoint) {
    previousLatitude = latitude;
    previousLongitude = longitude;
    havePreviousPoint = true;
    return;
  }

  const double segmentMeters = TinyGPSPlus::distanceBetween(
      latitude, longitude, previousLatitude, previousLongitude);

  // Update the reference on every acceptable-geometry fix. Reject tiny GNSS
  // jitter and implausible jumps instead of accumulating them as trip distance.
  if (gps.speed.isValid() && gps.speed.kmph() >= TripFilter::MIN_SPEED_KMH &&
      segmentMeters >= TripFilter::MIN_SEGMENT_METERS &&
      segmentMeters <= TripFilter::MAX_SEGMENT_METERS) {
    totalDistanceMeters += segmentMeters;
  }

  previousLatitude = latitude;
  previousLongitude = longitude;
}

// -----------------------------------------------------------------------------
// Wi-Fi, NTP and Firebase
// -----------------------------------------------------------------------------
void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpRequested && millis() - lastNtpAttempt >= 1000) {
      configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      ntpRequested = true;
      lastNtpAttempt = millis();
      Serial.println(F("NTP time synchronisation requested for TLS validation."));
    }
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiAttempt >= Timing::WIFI_RETRY_INTERVAL_MS) {
    lastWifiAttempt = now;
    Serial.println(F("Wi-Fi disconnected; retrying."));
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    ntpRequested = false;
  }
}

void initialiseFirebaseWhenReady() {
  if (firebaseStarted || firebaseStartAttempted || WiFi.status() != WL_CONNECTED) return;

  // TLS certificate validation needs a valid clock. Do not fall back to
  // setInsecure() if NTP or certificate provisioning fails.
  const time_t currentTime = time(nullptr);
  if (currentTime < 1700000000) return;

  const bool credentialsPresent =
      strlen(FIREBASE_API_KEY) > 10 &&
      strlen(FIREBASE_USER_EMAIL) > 5 &&
      strlen(FIREBASE_USER_PASSWORD) > 8 &&
      strlen(FIREBASE_DATABASE_URL) > 12;

  if (!credentialsPresent || strlen(FIREBASE_ROOT_CA_PEM) < 100) {
    firebaseStartAttempted = true;
    Serial.println(F("Firebase disabled: configure credentials and a trusted root CA in config.h."));
    return;
  }

  firebaseSslClient.setCACert(FIREBASE_ROOT_CA_PEM);
  firebaseSslClient.setX509Time(static_cast<uint32_t>(currentTime));
  firebaseSslClient.setBufferSizes(4096, 1024);

  initializeApp(firebaseAsyncClient, firebaseApp, getAuth(firebaseUserAuth),
                firebaseCallback, "firebaseAuthTask");
  firebaseApp.getApp<RealtimeDatabase>(firebaseDatabase);
  firebaseDatabase.url(FIREBASE_DATABASE_URL);

  firebaseStarted = true;
  Serial.println(F("Firebase client initialised; waiting for authentication."));
}

void firebaseCallback(AsyncResult &result) {
  if (!result.isResult()) return;

  if (result.isError()) {
    Serial.printf("Firebase task %s failed: %s (%d)\n",
                  result.uid().c_str(), result.error().message().c_str(),
                  result.error().code());
  }

  if (result.isDebug()) {
    Serial.printf("Firebase debug [%s]: %s\n",
                  result.uid().c_str(), result.debug().c_str());
  }

  if (result.available()) {
    if (result.uid().equals("navInstruction")) {
      String value = String(result.c_str());
      value.trim();
      if (value.length() >= 2 && value[0] == '"' && value[value.length() - 1] == '"') {
        value = value.substring(1, value.length() - 1);
      }
      value.replace("\\n", " ");
      if (value.length() > 0 && value != F("null")) {
        navigationInstruction = value;
      }
    } else {
      Serial.printf("Firebase task %s completed.\n", result.uid().c_str());
    }
  }
}

void uploadTelemetry(bool includeTrackPoint) {
  if (!firebaseStarted || !firebaseApp.ready() || !isFixFresh()) return;

  const double hdop = gps.hdop.isValid() ? gps.hdop.hdop() : -1.0;
  const double speedKmh = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  const double altitudeM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  const uint32_t satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const uint32_t gpsDate = gps.date.isValid() ? gps.date.value() : 0;
  const uint32_t gpsTime = gps.time.isValid() ? gps.time.value() : 0;

  char telemetryJson[420];
  snprintf(telemetryJson, sizeof(telemetryJson),
           "{\"latitude\":%.7f,\"longitude\":%.7f,\"speedKmh\":%.2f,"
           "\"courseDeg\":%.2f,\"altitudeM\":%.2f,\"tripDistanceM\":%.1f,"
           "\"maxSpeedKmh\":%.2f,\"satellitesUsed\":%lu,\"hdop\":%.2f,"
           "\"galileoSatellitesInView\":%u,\"galileoGsvValid\":%s,"
           "\"gpsDate\":%lu,\"gpsTime\":%lu,\"osnmaCommandSent\":%s,"
           "\"osnmaStatusReportSeen\":%s,\"updatedAtMs\":%lu}",
           gps.location.lat(), gps.location.lng(), speedKmh,
           gps.course.isValid() ? gps.course.deg() : 0.0, altitudeM,
           totalDistanceMeters, maximumSpeedKmh,
           static_cast<unsigned long>(satellites), hdop,
           static_cast<unsigned int>(galileoSatellitesInView),
           galileoSatellitesValid ? "true" : "false",
           static_cast<unsigned long>(gpsDate),
           static_cast<unsigned long>(gpsTime),
           osnmaCommandSent ? "true" : "false",
           osnmaStatusReportSeen ? "true" : "false",
           static_cast<unsigned long>(millis()));

  // PATCH the existing telemetry node rather than PUT/replace it. The track
  // history is a sibling child under /telemetrie_bici and must be preserved.
  firebaseDatabase.update<object_t>(firebaseAsyncClient, FIREBASE_TELEMETRY_PATH,
                                     object_t(telemetryJson), firebaseCallback,
                                     "telemetryUpdate");

  if (includeTrackPoint) {
    char trackJson[220];
    snprintf(trackJson, sizeof(trackJson),
             "{\"latitude\":%.7f,\"longitude\":%.7f,\"speedKmh\":%.2f,"
             "\"altitudeM\":%.2f,\"hdop\":%.2f,\"recordedAtMs\":%lu}",
             gps.location.lat(), gps.location.lng(), speedKmh, altitudeM, hdop,
             static_cast<unsigned long>(millis()));
    firebaseDatabase.push<object_t>(firebaseAsyncClient, FIREBASE_TRACK_PATH,
                                     object_t(trackJson), firebaseCallback,
                                     "trackPoint");
    trackPointPending = false;
  }
}

void fetchNavigationInstruction() {
  if (!firebaseStarted || !firebaseApp.ready()) return;
  firebaseDatabase.get(firebaseAsyncClient, FIREBASE_NAVIGATION_PATH,
                       firebaseCallback, false, "navInstruction");
}

// -----------------------------------------------------------------------------
// TFT presentation
// -----------------------------------------------------------------------------
void updateDisplay() {
  DisplayData data;
  data.fixValid = isFixFresh();
  data.speedKmh = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  data.tripDistanceM = totalDistanceMeters;
  data.maxSpeedKmh = maximumSpeedKmh;
  data.altitudeM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  data.hdopValid = gps.hdop.isValid();
  data.hdop = data.hdopValid ? gps.hdop.hdop() : 0.0;
  data.satellitesValid = gps.satellites.isValid();
  data.satellitesUsed = data.satellitesValid ? gps.satellites.value() : 0;
  data.galileoSatellitesValid = galileoSatellitesValid;
  data.galileoSatellitesInView = galileoSatellitesInView;
  data.wifiConnected = WiFi.status() == WL_CONNECTED;
  data.firebaseReady = firebaseStarted && firebaseApp.ready();
  data.osnmaCommandSent = osnmaCommandSent;
  data.osnmaStatusReportSeen = osnmaStatusReportSeen;
  data.navigationInstruction = navigationInstruction.c_str();
  displayUI.update(data);
}
