// JM-01: pickleball + F1 desk display for James.
// Board: Waveshare ESP32-S3-LCD-1.28 (round GC9A01, 240 x 240).
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "ui.h"
#include "net.h"

static AppData data;
static UiState state;

static void applyBrightness() {
  static int last = -1;
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  bool night = now > 1700000000 && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR);
  int v = night ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY;
  if (v != last) { displayBrightness(v); last = v; }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n%s firmware %s\n", DEVICE_NAME, FW_VERSION);
  setenv("TZ", TIMEZONE, 1);       // UK time for every date calculation
  tzset();

  displayBegin();
  uiBegin(&data, &state);
  uiRedrawNow();
  displayBrightness(BRIGHTNESS_DAY);
  netBegin(&data, &state);
}

void loop() {
  netLoop();
  uiLoop();
  displayLoop();
  applyBrightness();
  delay(5);
}
