#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "parse.h"
#include "ca_roots.h"

static AppData* D;
static UiState* S;
static SemaphoreHandle_t lock;
static Preferences prefs;
static WiFiManager wm;
static char driverBuf[8] = DEFAULT_DRIVER;
static WiFiManagerParameter pDriver("driver", "His F1 driver (3-letter code, e.g. VER, NOR, HAM)", driverBuf, 4);

static bool portalOpen = false, lanPortal = false, timeStarted = false;
static uint32_t portalSince = 0;
static volatile bool healthy = false;       // set after the first successful download
static bool pendingVerify = false;          // running a freshly installed update

void dataLock()   { if (!lock) lock = xSemaphoreCreateMutex(); xSemaphoreTake(lock, portMAX_DELAY); }
void dataUnlock() { xSemaphoreGive(lock); }

// Arduino would mark a new update as good straight away; we confirm it ourselves
// once it has connected and downloaded data (see netLoop).
extern "C" bool verifyRollbackLater() { return true; }

// ---------------------------------------------------------------------
//  Settings (driver code)
// ---------------------------------------------------------------------
static void saveParams() {
  String v = pDriver.getValue();
  v.trim(); v.toUpperCase();
  if (v.length() >= 3) {
    v = v.substring(0, 3);
    prefs.putString("driver", v);
    dataLock(); S->driver = v.c_str(); dataUnlock();
    Serial.printf("[settings] driver = %s\n", v.c_str());
  }
}

static void startPortal() {
  Serial.println("[wifi] opening setup network " SETUP_AP_NAME);
  wm.setConfigPortalBlocking(false);
  wm.startConfigPortal(SETUP_AP_NAME);
  portalOpen = true; portalSince = millis();
  S->mode = Mode::Setup;
}

static void onConnected() {
  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (S->mode == Mode::Setup) S->mode = Mode::Faces;
  if (!timeStarted) { configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com"); timeStarted = true; }
  if (!lanPortal) {
    MDNS.begin(MDNS_NAME);
    wm.startWebPortal();                     // settings page on his network: http://jm01.local
    MDNS.addService("http", "tcp", 80);
    lanPortal = true;
  }
}

// ---------------------------------------------------------------------
//  HTTPS downloads (background task)
// ---------------------------------------------------------------------
static bool repoConfigured() { return strcmp(GH_OWNER, "YOUR-GITHUB-NAME") != 0; }

static bool getJson(const char* url, JsonDocument& doc, JsonDocument* filter = nullptr) {
  WiFiClientSecure client;
  client.setCACert(CA_ROOTS);
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", DEVICE_NAME "/" FW_VERSION);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    DeserializationError e = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, body);
    ok = !e;
    if (e) Serial.printf("[net] JSON error %s for %s\n", e.c_str(), url);
  } else {
    Serial.printf("[net] HTTP %d for %s\n", code, url);
  }
  http.end();
  return ok;
}

static bool fetchSchedule() {
  if (!repoConfigured()) { Serial.println("[net] set GH_OWNER in config.h to load schedule.json"); return false; }
  JsonDocument doc;
  if (!getJson(SCHEDULE_URL, doc)) return false;
  dataLock(); AppData tmp = *D; dataUnlock();
  if (!parseSchedule(doc.as<JsonVariantConst>(), tmp)) return false;
  dataLock(); D->events = tmp.events; D->seeds = tmp.seeds; D->scheduleOk = true; dataUnlock();
  Serial.printf("[net] schedule: %u events\n", (unsigned)tmp.events.size());
  return true;
}

static bool fetchF1() {
  dataLock(); AppData tmp = *D; dataUnlock();
  bool ok = true;
  {
    JsonDocument doc;
    ok &= getJson(F1_BASE "/driverStandings.json", doc) && parseDriverStandings(doc.as<JsonVariantConst>(), tmp);
  }
  {
    JsonDocument doc;
    ok &= getJson(F1_BASE "/constructorStandings.json", doc) && parseConstructorStandings(doc.as<JsonVariantConst>(), tmp);
  }
  {
    // keep only what the faces use: the full season file is ~15 KB
    JsonDocument filter;
    JsonObject r = filter["MRData"]["RaceTable"]["Races"].add<JsonObject>();
    r["round"] = true; r["raceName"] = true; r["date"] = true; r["time"] = true;
    r["Circuit"]["Location"]["locality"] = true; r["Qualifying"] = true; r["Sprint"] = true;
    JsonDocument doc;
    ok &= getJson(F1_BASE ".json", doc, &filter) && parseRaces(doc.as<JsonVariantConst>(), tmp);
  }
  {
    JsonDocument filter;
    JsonObject res = filter["MRData"]["RaceTable"]["Races"][0]["Results"].add<JsonObject>();
    res["position"] = true; res["Driver"]["code"] = true;
    JsonDocument doc;
    ok &= getJson(F1_BASE "/last/results.json", doc, &filter) && parseLastResults(doc.as<JsonVariantConst>(), tmp);
  }
  dataLock();
  D->drivers = tmp.drivers; D->teams = tmp.teams; D->standingsRound = tmp.standingsRound;
  D->standingsOk = tmp.standingsOk; D->races = tmp.races; D->racesOk = tmp.racesOk; D->lastResults = tmp.lastResults;
  dataUnlock();
  Serial.printf("[net] F1: %u races, %u drivers, %u teams\n", (unsigned)tmp.races.size(), (unsigned)tmp.drivers.size(), (unsigned)tmp.teams.size());
  return ok;
}

// ---------------------------------------------------------------------
//  Over-the-air update from GitHub releases
// ---------------------------------------------------------------------
static bool newer(const char* tag, const char* cur) {
  int a[3] = {0}, b[3] = {0};
  sscanf(tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag, "%d.%d.%d", &a[0], &a[1], &a[2]);
  sscanf(cur, "%d.%d.%d", &b[0], &b[1], &b[2]);
  for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

static void otaCheck() {
  if (!repoConfigured()) return;
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject asset = filter["assets"].add<JsonObject>();
  asset["name"] = true; asset["browser_download_url"] = true;
  JsonDocument doc;
  if (!getJson(RELEASES_URL, doc, &filter)) return;
  const char* tag = doc["tag_name"] | "";
  String url;
  for (JsonVariantConst a : doc["assets"].as<JsonArrayConst>())
    if (strcmp(a["name"] | "", OTA_ASSET_NAME) == 0) url = a["browser_download_url"] | "";
  Serial.printf("[ota] latest release %s, running %s\n", tag, FW_VERSION);
  if (!*tag || url.isEmpty() || !newer(tag, FW_VERSION)) return;
  // a release that was installed and then rolled back is not tried again,
  // otherwise a bad release would install, fail, roll back and repeat forever
  if (prefs.getString("trying", "") == tag) { Serial.printf("[ota] %s was rolled back before, skipping\n", tag); return; }

  S->otaFrom = "v" FW_VERSION;
  S->otaTo = (tag[0] == 'v' || tag[0] == 'V') ? tag : (std::string("v") + tag);
  S->otaPct = 0; S->otaPhase = 0; S->mode = Mode::Updating;

  WiFiClientSecure client;
  client.setCACert(CA_ROOTS);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // GitHub redirects assets to its CDN
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) S->otaPct = (int)((int64_t)cur * 100 / total);
    if (cur >= total) S->otaPhase = 1;
  });
  prefs.putString("trying", tag);
  t_httpUpdate_return r = httpUpdate.update(client, url);
  if (r == HTTP_UPDATE_OK) {
    Serial.println("[ota] installed, restarting");
    S->otaPct = 100; S->otaPhase = 2;
    delay(2000);
    ESP.restart();
  }
  prefs.remove("trying");                      // download failed: fine to try again later
  Serial.printf("[ota] failed: %s\n", httpUpdate.getLastErrorString().c_str());
  S->mode = Mode::Faces;
}

static void netTask(void*) {
  uint32_t nextSchedule = 0, nextF1 = 0, nextOta = millis() + 60000;   // first update check a minute after boot
  int lastOtaDay = -1;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t t = millis();
      if ((int32_t)(t - nextSchedule) >= 0) {
        bool ok = fetchSchedule();
        nextSchedule = t + (ok ? SCHEDULE_REFRESH_MIN * 60000UL : 120000UL);
        if (ok) healthy = true;
      }
      if ((int32_t)(t - nextF1) >= 0) {
        bool ok = fetchF1();
        nextF1 = t + (ok ? F1_REFRESH_MIN * 60000UL : 120000UL);
        if (ok) healthy = true;
      }
      // update check: once a minute after boot, then daily at 03:xx UK time
      time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
      bool nightly = now > 1700000000 && tm.tm_hour == OTA_CHECK_HOUR && tm.tm_yday != lastOtaDay;
      if (((nextOta && (int32_t)(t - nextOta) >= 0) || nightly) && !pendingVerify) {
        nextOta = 0;
        if (nightly) lastOtaDay = tm.tm_yday;
        otaCheck();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ---------------------------------------------------------------------
void netBegin(AppData* data, UiState* state) {
  D = data; S = state;
  if (!lock) lock = xSemaphoreCreateMutex();

  prefs.begin("jm01", false);
  String drv = prefs.getString("driver", DEFAULT_DRIVER);
  strncpy(driverBuf, drv.c_str(), sizeof driverBuf - 1);
  S->driver = drv.c_str();
  pDriver.setValue(driverBuf, 4);

  // was this boot the first run of a new over-the-air update?
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK)
    pendingVerify = (st == ESP_OTA_IMG_PENDING_VERIFY);
  if (pendingVerify) Serial.println("[ota] new version: waiting until it proves it works");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_NAME);
  wm.setTitle(DEVICE_NAME);
  wm.setDarkMode(true);
  wm.addParameter(&pDriver);
  wm.setSaveParamsCallback(saveParams);
  static std::vector<const char*> menu = {"wifi", "param", "info", "restart"};
  wm.setMenu(menu);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_RETRY_PORTAL_MIN * 60);
  wm.setConfigPortalBlocking(false);

  if (wm.autoConnect(SETUP_AP_NAME)) onConnected();     // saved Wi-Fi works
  else { portalOpen = true; portalSince = millis(); S->mode = Mode::Setup; }

  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

void netLoop() {
  wm.process();                                 // setup portal or LAN settings page
  bool up = WiFi.status() == WL_CONNECTED;
  S->online = up;

  if (portalOpen) {
    if (up) { portalOpen = false; onConnected(); }
    else if (!wm.getConfigPortalActive()) {     // portal timed out: try saved Wi-Fi again
      portalOpen = false;
      Serial.println("[wifi] setup timed out, retrying saved Wi-Fi");
      WiFi.mode(WIFI_STA); WiFi.begin();
      portalSince = millis();
    }
  } else if (!up) {
    // Wi-Fi dropped (router reboot etc). The ESP32 reconnects by itself; if it is still
    // down after 2 minutes, open the setup network, which retries Wi-Fi after a few minutes.
    if (millis() - portalSince > 120000UL) {
      if (lanPortal) { wm.stopWebPortal(); lanPortal = false; }
      startPortal();
    }
  } else {
    portalSince = millis();
    if (!lanPortal) onConnected();
  }

  // a fresh update counts as good once it has downloaded data and run for a while
  if (pendingVerify) {
    if (healthy && millis() > HEALTHY_AFTER_SEC * 1000UL) {
      esp_ota_mark_app_valid_cancel_rollback();
      prefs.remove("trying");
      pendingVerify = false;
      Serial.println("[ota] new version confirmed");
    } else if (millis() > 10 * 60000UL) {
      Serial.println("[ota] new version never got online, rolling back");
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
  }
}
