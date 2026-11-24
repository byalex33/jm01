// Faces, ported from the browser preview (courtside_preview.html).
// Coordinates are in the 240 x 240 screen space; y values are text baselines.
#include "ui.h"
#include <Arduino.h>
#include <sys/time.h>
#include <cmath>
#include <cstring>
#include "config.h"
#include "display.h"
#include "parse.h"
#include "net.h"
#include "fonts/fonts.h"

// ---------- colours (same as the preview) ----------
namespace C {
  const uint32_t ink = 0xF2F4F7, dim = 0x8B95A5, faint = 0x262C36, ball = 0xDDF23A,
                 ppa = 0x3E7DF2, mlp = 0x22C38E, f1 = 0xF23B2F, live = 0xFF5A47, dark = 0x05070A;
}
static const char* WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* DOT = "\xC2\xB7";           // ·

static AppData* D;
static UiState* S;
static lv_layer_t layer;
static lv_layer_t* L = &layer;

// ---------- drawing primitives ----------
enum Align { CENTER, LEFT, RIGHT };

static int textW(const char* s, const lv_font_t* f, int sp = 0) {
  return lv_text_get_width(s, strlen(s), f, sp);
}
static void text(const char* s, int x, int y, const lv_font_t* f, uint32_t col,
                 Align al = CENTER, int sp = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = s; d.text_local = 1; d.font = f; d.color = lv_color_hex(col);
  d.opa = opa; d.letter_space = sp; d.align = LV_TEXT_ALIGN_LEFT; d.flag = LV_TEXT_FLAG_EXPAND;
  const int w = textW(s, f, sp) + 2;
  const int top = y - (f->line_height - f->base_line);
  lv_area_t a;
  a.x1 = al == CENTER ? x - w / 2 : al == LEFT ? x : x - w;
  a.x2 = a.x1 + w; a.y1 = top; a.y2 = top + f->line_height;
  lv_draw_label(L, &d, &a);
}
static void text(const std::string& s, int x, int y, const lv_font_t* f, uint32_t col,
                 Align al = CENTER, int sp = 0, lv_opa_t opa = LV_OPA_COVER) {
  text(s.c_str(), x, y, f, col, al, sp, opa);
}
// shorten with an ellipsis so it fits in max px (UTF-8 aware)
static std::string fit(std::string s, const lv_font_t* f, int max, int sp = 0) {
  if (textW(s.c_str(), f, sp) <= max) return s;
  while (!s.empty()) {
    do { s.pop_back(); } while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80);
    if (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0xC0) s.pop_back();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (textW((s + "\xE2\x80\xA6").c_str(), f, sp) <= max) break;
  }
  return s + "\xE2\x80\xA6";
}
static void rect(int x, int y, int w, int h, uint32_t col, int radius = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(col); d.bg_opa = opa; d.radius = radius;
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(L, &d, &a);
}
static void dot(int cx, int cy, int r, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, col, LV_RADIUS_CIRCLE, opa);
}
static void line(float x1, float y1, float x2, float y2, uint32_t col, int w, bool round = true, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
  d.color = lv_color_hex(col); d.width = w; d.opa = opa;
  d.round_start = round; d.round_end = round;
  lv_draw_line(L, &d);
}
// arc around the screen centre; angles in degrees, 0 = right, clockwise; r = centre of the stroke
static void arcDeg(int r, int a0, int a1, uint32_t col, int w, int cx = 120, int cy = 120) {
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.center.x = cx; d.center.y = cy; d.radius = r + w / 2; d.width = w;
  d.start_angle = a0; d.end_angle = a1; d.color = lv_color_hex(col); d.rounded = 1;
  lv_draw_arc(L, &d);
}
// progress ring from 12 o'clock
static void ring(int r, float frac, uint32_t col, int w) {
  arcDeg(r, 0, 360, C::faint, w);
  if (frac > 0.003f) {
    if (frac > 1) frac = 1;
    int end = 270 + (int)lroundf(frac * 360);
    arcDeg(r, 270, end > 360 ? end - 360 : end, col, w);
  }
}
static int halfChord(int y) { int dy = y - 120; return (int)sqrtf((float)std::max(0, 120 * 120 - dy * dy)); }

struct Run { std::string s; const lv_font_t* f; uint32_t col; int sp; };
static void runs(int y, std::initializer_list<Run> items, int gap = 6) {
  int total = 0, n = 0;
  for (auto& r : items) { total += textW(r.s.c_str(), r.f, r.sp); n++; }
  int x = 120 - (total + gap * (n - 1)) / 2;
  for (auto& r : items) { text(r.s, x, y, r.f, r.col, LEFT, r.sp); x += textW(r.s.c_str(), r.f, r.sp) + gap; }
}
static void pill(int cx, int cy, const char* s, uint32_t col) {
  const int w = textW(s, &f_b7_11, 2) + 20, h = 20;
  rect(cx - w / 2, cy - h / 2, w, h, col, h / 2);
  text(s, cx + 1, cy + 4, &f_b7_11, C::dark, CENTER, 2);
}

// ---------- time ----------
static int64_t nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
static struct tm localTm(time_t t) { struct tm r; localtime_r(&t, &r); return r; }
static std::string fmt(const char* f, ...) {
  char b[96]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return b;
}
static lv_opa_t pulse(int64_t ms) { return (lv_opa_t)(255 * (0.55f + 0.45f * sinf(ms / 260.0f))); }
static std::string pts(float p) { return (p == floorf(p)) ? fmt("%d", (int)p) : fmt("%.1f", p); }
static uint32_t tourCol(const std::string& t) { return t == "PPA" ? C::ppa : C::mlp; }

static std::vector<const PbEvent*> upcoming(time_t now) {
  std::vector<const PbEvent*> v;
  for (auto& e : D->events) if (e.end > now) v.push_back(&e);
  return v;
}
// whole calendar days from today (UK) to t
static int dayDiff(time_t now, time_t t) {
  struct tm a = localTm(now); a.tm_hour = a.tm_min = a.tm_sec = 0; a.tm_isdst = -1;
  return (int)floor(difftime(t, mktime(&a)) / 86400.0 + 0.01);
}
static std::string dateRange(time_t s, time_t eEx) {
  struct tm a = localTm(s), b = localTm(eEx - 43200);
  if (a.tm_mon == b.tm_mon) return fmt("%d\xE2\x80\x93%d %s", a.tm_mday, b.tm_mday, MON[a.tm_mon]);
  return fmt("%d %s \xE2\x80\x93 %d %s", a.tm_mday, MON[a.tm_mon], b.tm_mday, MON[b.tm_mon]);
}
struct F1Next { const Race* race; const Session* cur; const Session* raceSession; };
static bool f1Next(time_t now, F1Next& out) {
  for (auto& r : D->races) for (auto& s : r.sessions)
    if (s.start + s.durMin * 60 > now) { out = {&r, &s, &r.sessions.back()}; return true; }
  return false;
}
static void waiting(const char* what) {
  text(what, 120, 116, &f_b6_12, C::dim, CENTER, 2);
  text(S->online ? "LOADING" : "OFFLINE", 120, 136, &f_b6_10, C::faint, CENTER, 2);
}

// =====================================================================
//  Faces
// =====================================================================
static void faceClock(time_t now, int64_t ms) {
  struct tm t = localTm(now);
  for (int i = 0; i < 60; i++) {
    float a = i / 60.0f * 2 * PI - PI / 2; bool major = i % 5 == 0;
    int r0 = major ? 102 : 107, r1 = 111;
    line(120 + cosf(a) * r0, 120 + sinf(a) * r0, 120 + cosf(a) * r1, 120 + sinf(a) * r1,
         major ? C::dim : C::faint, major ? 2 : 1, false);
  }
  float sec = t.tm_sec + (ms % 1000) / 1000.0f;
  int end = 270 + (int)(sec / 60 * 360);
  if (sec > 0.2f) arcDeg(116, 270, end > 360 ? end - 360 : end, C::ball, 3);
  text(DEVICE_NAME, 120, 50, &f_b6_9, C::dim, CENTER, 3);
  text(fmt("%s %d %s", WD[t.tm_wday], t.tm_mday, MON[t.tm_mon]), 120, 74, &f_b6_14, C::dim, CENTER, 2);
  text(fmt("%02d:%02d", t.tm_hour, t.tm_min), 120, 136, &f_c7_80, C::ink);

  auto ev = upcoming(now);
  if (!ev.empty()) {
    const PbEvent& e = *ev[0];
    bool live = now >= e.start; int d = dayDiff(now, e.start);
    std::string tail = live ? "LIVE" : d <= 0 ? "TODAY" : d == 1 ? "TOMORROW" : fmt("%dD", d);
    runs(166, {{e.tour, &f_b7_12, tourCol(e.tour), 1},
               {fit(utf8Upper(e.name), &f_b5_13, 104, 1), &f_b5_13, C::ink, 1},
               {tail, &f_b6_13, live ? C::live : C::dim, 1}}, 7);
  }
  F1Next n;
  if (f1Next(now, n)) {
    long diff = (long)(n.raceSession->start - now); bool live = now >= n.cur->start;
    std::string tail = live ? "LIVE" : diff >= 86400 ? fmt("%ldD %ldH", diff / 86400, diff % 86400 / 3600)
                                                     : fmt("%ldH %ldM", diff / 3600, diff % 3600 / 60);
    runs(189, {{"F1", &f_b7_12, C::f1, 1}, {fit(n.race->shortName, &f_b5_13, 110, 1), &f_b5_13, C::ink, 1},
               {tail, &f_b6_13, live ? C::live : C::dim, 1}}, 7);
  }
}

static void court(uint32_t col) {
  const float k = 4.3f, w = 20 * k, h = 44 * k, x0 = 120 - w / 2, y0 = 120 - h / 2, kit = 7 * k;
  const lv_opa_t o = 46;
  line(x0, y0, x0 + w, y0, col, 1, false, o); line(x0, y0 + h, x0 + w, y0 + h, col, 1, false, o);
  line(x0, y0, x0, y0 + h, col, 1, false, o); line(x0 + w, y0, x0 + w, y0 + h, col, 1, false, o);
  line(x0, 120 - kit, x0 + w, 120 - kit, col, 1, false, o); line(x0, 120 + kit, x0 + w, 120 + kit, col, 1, false, o);
  line(120, y0, 120, 120 - kit, col, 1, false, o); line(120, 120 + kit, 120, y0 + h, col, 1, false, o);
  line(x0 - 8, 120, x0 + w + 8, 120, col, 3, false, 80);
}

static void faceNext(time_t now, int64_t ms) {
  if (!D->scheduleOk) return waiting("PICKLEBALL");
  auto ev = upcoming(now);
  if (ev.empty()) { text("OFF SEASON", 120, 128, &f_c7_32, C::ink, CENTER, 1); return; }
  const PbEvent& e = *ev[0]; uint32_t col = tourCol(e.tour);
  court(col);
  pill(120, 46, e.tour == "PPA" ? "PPA TOUR" : e.tour.c_str(), col);
  text(fit(e.name, &f_c6_28, 170), 120, 88, &f_c6_28, C::ink);
  if (now >= e.start) {
    int total = (int)lround(difftime(e.end, e.start) / 86400.0), day = (int)(difftime(now, e.start) / 86400) + 1;
    ring(114, (float)difftime(now, e.start) / (float)difftime(e.end, e.start), col, 4);
    text(fmt("DAY %d", day), 120, 150, &f_c7_64, C::ink);
    std::string a = fmt("OF %d  %s  ", total, DOT);
    int wa = textW(a.c_str(), &f_b6_12, 2), wl = textW("LIVE", &f_b6_12, 2), x = 120 - (wa + wl) / 2;
    dot(x - 9, 172, 3, C::live, pulse(ms));
    text(a, x, 176, &f_b6_12, C::dim, LEFT, 2);
    text("LIVE", x + wa, 176, &f_b6_12, C::live, LEFT, 2);
  } else {
    int d = dayDiff(now, e.start);
    if (d >= 1) {
      text(fmt("%d", d), 120, 156, &f_c7_88, C::ink);
      text(d == 1 ? "DAY TO GO" : "DAYS TO GO", 120, 177, &f_b6_12, C::dim, CENTER, 2);
    } else {
      int h = std::max(1, (int)ceil(difftime(e.start, now) / 3600));
      text(fmt("%d", h), 120, 156, &f_c7_88, C::ink);
      text(h == 1 ? "HOUR TO GO" : "HOURS TO GO", 120, 177, &f_b6_12, C::dim, CENTER, 2);
    }
  }
  text(fit(utf8Upper(e.city), &f_b5_12, 160, 1), 120, 201, &f_b5_12, C::dim, CENTER, 1);
  text(dateRange(e.start, e.end), 120, 218, &f_b6_13, C::ink, CENTER, 1);
}

static void faceList(time_t now, int64_t ms) {
  text("UP NEXT", 120, 40, &f_b6_12, C::dim, CENTER, 3);
  rect(104, 47, 32, 2, C::faint);
  if (!D->scheduleOk) return waiting("PICKLEBALL");
  auto ev = upcoming(now);
  if (ev.empty()) { text("NOTHING SCHEDULED", 120, 128, &f_b6_14, C::dim, CENTER, 1); return; }
  for (size_t i = 0; i < ev.size() && i < 3; i++) {
    const PbEvent& e = *ev[i]; int cy = 82 + (int)i * 48; uint32_t col = tourCol(e.tour);
    struct tm s = localTm(e.start);
    if (now >= e.start) {
      dot(62, cy - 9, 3, C::live, pulse(ms));
      text("LIVE", 62, cy + 11, &f_b7_12, C::live, CENTER, 1);
    } else {
      text(MON[s.tm_mon], 62, cy - 6, &f_b6_10, C::dim, CENTER, 2);
      text(fmt("%02d", s.tm_mday), 62, cy + 15, &f_c7_24, C::ink);
    }
    rect(84, cy - 14, 3, 30, col);
    int right = 120 + halfChord(cy + 14) - 20, max = right - 96;
    text(fit(e.name, &f_c6_20, max), 96, cy + 2, &f_c6_20, C::ink, LEFT);
    text(fit(e.tour + "  " + DOT + "  " + utf8Upper(e.city), &f_b5_10, max, 1), 96, cy + 17, &f_b5_10, C::dim, LEFT, 1);
    if (i < 2 && i + 1 < ev.size()) rect(44, cy + 25, 152, 1, C::faint);
  }
}

static void faceSeeds(time_t now, int64_t) {
  text("TOP SEEDS", 120, 40, &f_b6_12, C::dim, CENTER, 3);
  if (!D->seeds.valid) return waiting("SEEDS");
  uint32_t col = C::ppa;
  for (auto& e : D->events) if (e.name == D->seeds.event) col = tourCol(e.tour);
  text(utf8Upper(D->seeds.event), 120, 57, &f_b7_11, col, CENTER, 2);
  const char* labels[] = {"MEN'S SINGLES", "WOMEN'S SINGLES", "MEN'S DOUBLES", "WOMEN'S DOUBLES"};
  const std::string* names[] = {&D->seeds.ms, &D->seeds.ws, &D->seeds.md, &D->seeds.wd};
  for (int i = 0; i < 4; i++) {
    int cy = 86 + i * 34;
    text(labels[i], 120, cy - 5, &f_b6_9, C::dim, CENTER, 2);
    text(fit(*names[i], &f_c6_20, 2 * halfChord(cy + 12) - 44), 120, cy + 13, &f_c6_20, C::ink);
  }
}

static void faceF1(time_t now, int64_t ms) {
  if (!D->racesOk) return waiting("FORMULA 1");
  F1Next n;
  if (!f1Next(now, n)) {
    text("SEASON OVER", 120, 124, &f_c7_32, C::ink, CENTER, 1);
    text("BACK IN MARCH", 120, 146, &f_b6_12, C::dim, CENTER, 2);
    return;
  }
  bool live = now >= n.cur->start;
  ring(113, 1.0f - (float)difftime(n.raceSession->start, now) / (7 * 86400.0f), C::f1, 3);
  runs(48, {{"F1", &f_b7_13, C::f1, 2}, {DOT, &f_b6_12, C::dim, 0}, {fmt("ROUND %d", n.race->round), &f_b6_12, C::dim, 2}}, 6);
  text(fit(n.race->shortName, &f_c7_32, 180, 1), 120, 86, &f_c7_32, C::ink, CENTER, 1);
  text(utf8Upper(n.race->locality), 120, 105, &f_b5_11, C::dim, CENTER, 2);
  if (live) {
    text("LIVE", 120, 154, &f_c7_50, C::live, CENTER, 2, pulse(ms));
    text(n.cur->kind + " IN PROGRESS", 120, 174, &f_b6_10, C::dim, CENTER, 2);
  } else {
    long diff = (long)(n.cur->start - now);
    long v[3]; const char* l[3]; int g;
    if (diff >= 3600) { v[0] = diff / 86400; v[1] = diff % 86400 / 3600; v[2] = diff % 3600 / 60; l[0] = "DAYS"; l[1] = "HRS"; l[2] = "MIN"; g = 3; }
    else { v[0] = diff / 60; v[1] = diff % 60; l[0] = "MIN"; l[1] = "SEC"; g = 2; }
    const int step = 58, x0 = 120 - step * (g - 1) / 2;
    for (int i = 0; i < g; i++) {
      int cx = x0 + i * step;
      text(fmt("%02ld", v[i]), cx, 153, &f_c7_50, C::ink);
      text(l[i], cx, 173, &f_b6_10, C::dim, CENTER, 2);
      if (i) text(":", cx - step / 2, 150, &f_c7_50, C::faint);
    }
  }
  struct tm st = localTm(n.cur->start);
  runs(199, {{n.cur->kind, &f_b7_13, C::ink, 1}, {fmt("%s %02d:%02d", WD[st.tm_wday], st.tm_hour, st.tm_min), &f_b5_13, C::dim, 1}}, 8);
  for (int row = 0; row < 2; row++) for (int c = 0; c < 10; c++)
    rect(100 + c * 4, 210 + row * 4, 4, 4, (row + c) % 2 ? 0x1A1D22 : C::ink);
}

struct TableRow { std::string name, sub, pts; uint32_t col; bool me; };
static void table(const char* title, const std::vector<TableRow>& rows, const std::string& foot) {
  runs(40, {{title, &f_b6_12, C::ink, 3}, {fmt("AFTER RD %d", D->standingsRound), &f_b6_10, C::dim, 2}}, 10);
  const int right = 120 + halfChord(78 + 2 * 44 + 14) - 20, max = right - 80 - 34;
  for (size_t i = 0; i < rows.size(); i++) {
    const TableRow& r = rows[i]; int cy = 78 + (int)i * 44;
    text(fmt("%d", (int)i + 1), 54, cy + 10, &f_c7_28, i == 0 ? C::ink : C::dim);
    rect(68, cy - 13, 3, 29, r.col);
    text(fit(r.name, &f_c6_20, max), 78, cy + 2, &f_c6_20, r.me ? C::ball : C::ink, LEFT);
    text(fit(r.sub, &f_b5_10, max, 1), 78, cy + 16, &f_b5_10, C::dim, LEFT, 1);
    text(r.pts, right, cy + 3, &f_c7_24, C::ink, RIGHT);
    text("PTS", right, cy + 15, &f_b6_9, C::dim, RIGHT, 1);
    if (i + 1 < rows.size()) rect(44, cy + 24, 152, 1, C::faint);
  }
  text(foot, 120, 210, &f_b6_10, C::dim, CENTER, 2);
}

static void faceDrivers(time_t, int64_t) {
  if (!D->standingsOk || D->drivers.size() < 2) return waiting("DRIVERS");
  std::vector<TableRow> rows;
  for (size_t i = 0; i < 3 && i < D->drivers.size(); i++) {
    auto& d = D->drivers[i];
    rows.push_back({d.last, utf8Upper(d.teamName), pts(d.points), teamColour(d.teamId), d.code == S->driver});
  }
  table("DRIVERS", rows, fmt("%s LEADS BY %s", D->drivers[0].code.c_str(), pts(D->drivers[0].points - D->drivers[1].points).c_str()));
}

static void faceTeams(time_t, int64_t) {
  if (D->teams.size() < 2) return waiting("TEAMS");
  std::vector<TableRow> rows;
  for (size_t i = 0; i < 3 && i < D->teams.size(); i++) {
    auto& t = D->teams[i];
    rows.push_back({t.name, t.codes, pts(t.points), teamColour(t.id), false});
  }
  table("TEAMS", rows, "GAP TO P2  " + pts(D->teams[0].points - D->teams[1].points) + " PTS");
}

static void faceMine(time_t, int64_t) {
  if (!D->standingsOk || D->drivers.size() < 2) return waiting("YOUR DRIVER");
  const DriverRow* d = nullptr;
  for (auto& r : D->drivers) if (r.code == S->driver) d = &r;
  if (!d) {
    text("DRIVER NOT FOUND", 120, 112, &f_b6_12, C::dim, CENTER, 2);
    text(S->driver, 120, 140, &f_c7_32, C::ink, CENTER, 1);
    text("CHANGE IT AT " MDNS_NAME ".LOCAL", 120, 164, &f_b6_9, C::faint, CENTER, 2);
    return;
  }
  const DriverRow& lead = D->drivers[0];
  uint32_t tc = teamColour(d->teamId);
  ring(112, lead.points > 0 ? d->points / lead.points : 0, tc, 4);
  text(utf8Upper(d->teamName), 120, 46, &f_b7_11, tc, CENTER, 2);
  text(utf8Upper(d->first), 120, 70, &f_b5_12, C::dim, CENTER, 2);
  text(fit(utf8Upper(d->last), &f_c7_32, 184, 1), 120, 99, &f_c7_32, C::ink, CENTER, 1);
  text(fmt("P%d", d->pos), 120, 158, &f_c7_64, C::ink);
  text("IN THE CHAMPIONSHIP", 120, 175, &f_b6_9, C::dim, CENTER, 2);
  std::string gap = d == &lead ? "+" + pts(d->points - D->drivers[1].points) + " AHEAD"
                               : pts(lead.points - d->points) + " BEHIND";
  runs(197, {{pts(d->points) + " PTS", &f_b7_13, C::ink, 1}, {DOT, &f_b5_13, C::dim, 0}, {gap, &f_b5_13, C::dim, 1}}, 7);
  int last = 0;
  for (auto& r : D->lastResults) if (r.code == d->code) last = r.pos;
  if (last) runs(214, {{"LAST RACE", &f_b6_9, C::dim, 2}, {fmt("P%d", last), &f_b7_10, last == 1 ? C::ball : C::ink, 1}}, 6);
}

static void faceSetup(time_t, int64_t) {
  for (int r : {12, 22, 32}) arcDeg(r - 2, 225, 315, C::ppa, 4, 120, 86);
  dot(120, 86, 3, C::ppa);
  text("SET UP WI-FI", 120, 122, &f_c7_32, C::ink, CENTER, 1);
  struct Step { const char* a; uint32_t ca; const lv_font_t* fa; const char* b; };
  Step steps[] = {{"Join", C::dim, &f_b5_13, SETUP_AP_NAME}, {"Choose your home Wi-Fi", C::ink, &f_b5_13, ""},
                  {"Done, it remembers it", C::ink, &f_b5_13, ""}};
  int maxW = 0;
  for (auto& s : steps) maxW = std::max(maxW, textW(s.a, s.fa) + (*s.b ? 5 + textW(s.b, &f_b7_13) : 0));
  int left = 120 - (24 + maxW) / 2;
  for (int i = 0; i < 3; i++) {
    int y = 150 + i * 22;
    dot(left + 8, y - 4, 8, C::faint);
    text(fmt("%d", i + 1), left + 8, y, &f_b7_11, C::ink);
    text(steps[i].a, left + 24, y, steps[i].fa, steps[i].ca, LEFT);
    if (*steps[i].b) text(steps[i].b, left + 24 + textW(steps[i].a, steps[i].fa) + 5, y, &f_b7_13, C::ink, LEFT);
  }
  text("2.4 GHZ WI-FI ONLY", 120, 218, &f_b6_9, C::dim, CENTER, 2);
}

static void faceUpdate(time_t, int64_t ms) {
  ring(112, S->otaPct / 100.0f, C::ppa, 5);
  line(120, 40, 120, 56, C::ppa, 3); line(113, 50, 120, 57, C::ppa, 3); line(127, 50, 120, 57, C::ppa, 3);
  rect(111, 61, 18, 3, C::ppa);
  text("UPDATING", 120, 84, &f_b6_12, C::dim, CENTER, 3);
  std::string n = fmt("%d", S->otaPct);
  int wn = textW(n.c_str(), &f_c7_72), wp = textW("%", &f_c7_32), x = 120 - (wn + wp + 2) / 2;
  text(n, x, 142, &f_c7_72, C::ink, LEFT);
  text("%", x + wn + 2, 142, &f_c7_32, C::dim, LEFT);
  if (S->otaPhase == 2) {
    for (int i = 0; i < 3; i++) dot(108 + i * 12, 162, 3, C::ink, (lv_opa_t)(64 + 191 * std::max(0.0f, sinf(ms / 220.0f - i * 0.9f))));
  } else text(S->otaPhase == 0 ? "DOWNLOADING" : "INSTALLING", 120, 166, &f_b6_11, C::ink, CENTER, 2);
  // "v1.0.0 -> v1.1.0" with a drawn arrow (Barlow has no arrow glyph)
  int wa = textW(S->otaFrom.c_str(), &f_b5_13), wb = textW(S->otaTo.c_str(), &f_b7_13), xa = 120 - (wa + wb + 30) / 2;
  text(S->otaFrom, xa, 189, &f_b5_13, C::dim, LEFT);
  int ax = xa + wa + 7;
  line(ax, 185, ax + 15, 185, C::dim, 1); line(ax + 11, 182, ax + 15, 185, C::dim, 1); line(ax + 11, 188, ax + 15, 185, C::dim, 1);
  text(S->otaTo, ax + 23, 189, &f_b7_13, C::ink, LEFT);
  text("DON'T UNPLUG", 120, 206, &f_b6_9, C::dim, CENTER, 2);
}

// =====================================================================
typedef void (*FaceFn)(time_t, int64_t);
static const FaceFn ROTATION[] = {faceClock, faceNext, faceList, faceSeeds, faceF1, faceDrivers, faceTeams, faceMine};
static const int N_FACES = sizeof(ROTATION) / sizeof(ROTATION[0]);
static int face = 0;
static uint32_t faceSince = 0, lastFrame = 0, fadeStart = 0;
static Mode lastMode = Mode::Faces;

static void drawFrame() {
  lv_obj_t* cv = displayCanvas();
  lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
  lv_canvas_init_layer(cv, &layer);
  int64_t ms = nowMs(); time_t now = (time_t)(ms / 1000);
  dataLock();
  switch (S->mode) {
    case Mode::Setup:    faceSetup(now, ms); break;
    case Mode::Updating: faceUpdate(now, ms); break;
    default:
      if (now < 1700000000) waiting(S->online ? "SETTING THE TIME" : "CONNECTING");   // clock not synced yet
      else ROTATION[face](now, ms);
      break;
  }
  dataUnlock();
  lv_canvas_finish_layer(cv, &layer);
}

void uiBegin(AppData* data, UiState* state) { D = data; S = state; faceSince = millis(); }

void uiNextFace() { face = (face + 1) % N_FACES; faceSince = fadeStart = millis(); }
void uiSetFace(int i) { face = i % N_FACES; faceSince = fadeStart = millis(); }
int uiFaceCount() { return N_FACES; }

void uiRedrawNow() { drawFrame(); displayLoop(); }

void uiLoop() {
  uint32_t t = millis();
  if (S->mode != lastMode) { lastMode = S->mode; fadeStart = t; }
  if (S->mode == Mode::Faces && t - faceSince > FACE_SECONDS * 1000UL) uiNextFace();
  if (t - lastFrame >= 100) {
    lastFrame = t;
    // short fade-in after each face change
    uint32_t f = t - fadeStart;
    lv_obj_set_style_opa(displayCanvas(), f >= 250 ? LV_OPA_COVER : (lv_opa_t)(f * 255 / 250), 0);
    drawFrame();
  }
}
