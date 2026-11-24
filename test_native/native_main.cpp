// PC check for JM-01, no board needed.
//  1. Runs the real parsers on saved Jolpica responses + an example schedule.json
//  2. Renders every face with the real firmware drawing code (LVGL) to PPM images
// Usage: jm01_native <dir with current*.json> <schedule.json> <out dir> [driver code, default VER]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "../src/model.h"
#include "../src/parse.h"
#include "../src/ui.h"
#include "../src/display.h"
#include "../src/net.h"

// ---- stand-ins for the hardware parts ----
static auto t0 = std::chrono::steady_clock::now();
uint32_t millis() { return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); }
static lv_obj_t* canvas;
static uint16_t* pixels;
lv_obj_t* displayCanvas() { return canvas; }
void displayLoop() {}
void displayBrightness(uint8_t) {}
void displayBegin() {}
void dataLock() {}
void dataUnlock() {}
static void flush(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }
static uint32_t tick() { return millis(); }

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

template <typename F> static bool run(const char* name, const std::string& path, AppData& d, F fn) {
  JsonDocument doc;
  std::string body = slurp(path);
  if (body.empty()) { printf("  %-22s MISSING %s\n", name, path.c_str()); return false; }
  auto e = deserializeJson(doc, body);
  bool ok = !e && fn(doc.as<JsonVariantConst>(), d);
  printf("  %-22s %s\n", name, ok ? "ok" : e ? e.c_str() : "parse failed");
  return ok;
}

static void savePPM(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint16_t c = pixels[i];
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 4) { printf("usage: %s <jolpica dir> <schedule.json> <out dir> [driver]\n", argv[0]); return 2; }
  std::string dir = argv[1], out = argv[3];
  AppData data; UiState state;
  printf("parsers:\n");
  bool ok = true;
  ok &= run("schedule.json", argv[2], data, parseSchedule);
  ok &= run("driverStandings", dir + "/current_driverStandings.json", data, parseDriverStandings);
  ok &= run("constructorStandings", dir + "/current_constructorStandings.json", data, parseConstructorStandings);
  ok &= run("races", dir + "/current.json", data, parseRaces);
  ok &= run("last results", dir + "/current_last_results.json", data, parseLastResults);

  printf("\nsummary:\n  %u pickleball events, seeds %s\n", (unsigned)data.events.size(), data.seeds.valid ? data.seeds.event.c_str() : "none");
  printf("  %u races, last: R%d %s (%s), sessions %u\n", (unsigned)data.races.size(), data.races.back().round,
         data.races.back().shortName.c_str(), data.races.back().locality.c_str(), (unsigned)data.races.back().sessions.size());
  for (size_t i = 0; i < 3 && i < data.drivers.size(); i++)
    printf("  P%d %s %s %s (%s) %.0f\n", data.drivers[i].pos, data.drivers[i].code.c_str(), data.drivers[i].first.c_str(),
           data.drivers[i].last.c_str(), data.drivers[i].teamName.c_str(), data.drivers[i].points);
  for (size_t i = 0; i < 3 && i < data.teams.size(); i++)
    printf("  T%d %s [%s] %.0f\n", data.teams[i].pos, data.teams[i].name.c_str(), data.teams[i].codes.c_str(), data.teams[i].points);
  printf("  last race results: %u, winner %s\n", (unsigned)data.lastResults.size(), data.lastResults.empty() ? "-" : data.lastResults[0].code.c_str());

  // ---- render every face ----
  lv_init();
  lv_tick_set_cb(tick);
  lv_display_t* disp = lv_display_create(240, 240);
  static uint8_t buf[240 * 40 * 2];
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_buffers(disp, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  pixels = (uint16_t*)malloc(240 * 240 * 2);
  canvas = lv_canvas_create(lv_screen_active());
  lv_canvas_set_buffer(canvas, pixels, 240, 240, LV_COLOR_FORMAT_RGB565);

  state.online = true;
  state.driver = argc > 4 ? argv[4] : "VER";
  uiBegin(&data, &state);
  for (int i = 0; i < uiFaceCount(); i++) {
    uiSetFace(i);
    uiRedrawNow();
    savePPM(out + "/face" + std::to_string(i) + ".ppm");
  }
  state.mode = Mode::Setup; uiRedrawNow(); savePPM(out + "/face_setup.ppm");
  state.mode = Mode::Updating; state.otaPct = 62; state.otaFrom = "v1.0.0"; state.otaTo = "v1.1.0";
  uiRedrawNow(); savePPM(out + "/face_update.ppm");
  printf("\nrendered %d faces to %s\n", uiFaceCount() + 2, out.c_str());
  return ok ? 0 : 1;
}
