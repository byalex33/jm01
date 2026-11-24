// =====================================================================
//  JM-01 configuration. Everything you might want to change is here.
// =====================================================================
#pragma once

// ---- Version. Bump this for every release you publish on GitHub. ----
// The device updates itself when a GitHub release tag is newer than this.
#define FW_VERSION        "1.0.0"

// ---- Your GitHub repository (fill these in) -------------------------
// Create a PUBLIC repo, put schedule.json in it, and publish firmware
// releases there with an asset named exactly "firmware.bin".
#define GH_OWNER          "byalex33"
#define GH_REPO           "jm01"
#define GH_BRANCH         "main"
#define SCHEDULE_URL      "https://raw.githubusercontent.com/" GH_OWNER "/" GH_REPO "/" GH_BRANCH "/schedule.json"
#define RELEASES_URL      "https://api.github.com/repos/" GH_OWNER "/" GH_REPO "/releases/latest"
#define OTA_ASSET_NAME    "firmware.bin"

// ---- F1 data (free, no key) -----------------------------------------
#define F1_BASE           "https://api.jolpi.ca/ergast/f1/current"

// ---- Device ----------------------------------------------------------
#define DEVICE_NAME       "JM-01"
#define SETUP_AP_NAME     "JM-01-SETUP"     // Wi-Fi network shown during setup
#define MDNS_NAME         "jm01"            // settings page: http://jm01.local
#define DEFAULT_DRIVER    "VER"

// UK time with automatic summer time (last Sunday of March / October)
#define TIMEZONE          "GMT0BST,M3.5.0/1,M10.5.0"

// ---- Display (Waveshare ESP32-S3-LCD-1.28, non-touch) -----------------
#define PIN_LCD_DC        8
#define PIN_LCD_CS        9
#define PIN_LCD_SCK       10
#define PIN_LCD_MOSI      11
#define PIN_LCD_RST       12
#define PIN_LCD_BL        40
#define LCD_SPI_HZ        80000000
// The USB tab points DOWN in the wedge. If the picture is upside down or
// sideways on the real board, try 0, 1, 2 or 3 here.
#define LCD_ROTATION      2

// Backlight 0-255, dimmer at night
#define BRIGHTNESS_DAY    200
#define BRIGHTNESS_NIGHT  40
#define NIGHT_START_HOUR  23
#define NIGHT_END_HOUR    7

// ---- Timing ------------------------------------------------------------
#define FACE_SECONDS          8              // each face shows for this long
#define SCHEDULE_REFRESH_MIN  60
#define F1_REFRESH_MIN        180
#define OTA_CHECK_HOUR        3              // daily update check at 03:xx UK time
#define WIFI_RETRY_PORTAL_MIN 3              // setup network stays up this long, then retries Wi-Fi
#define HEALTHY_AFTER_SEC     90             // a new version must run this long and get online
