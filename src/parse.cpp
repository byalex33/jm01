#include "parse.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string str(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? std::string(s) : std::string();
}
static float num(JsonVariantConst v) {          // Jolpica sends numbers as strings
  if (v.is<const char*>()) return (float)atof(v.as<const char*>());
  return v.as<float>();
}

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

time_t utcToEpoch(int y, int mo, int d, int h, int mi, int s) {
  return (time_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

time_t ukMidnight(int y, int m, int d) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_isdst = -1;
  return mktime(&t);                              // local time = UK (TZ set at boot)
}

static bool parseDate(const std::string& s, int& y, int& m, int& d) {
  return sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3;
}

// "2026-11-29" + "16:00:00Z" -> epoch. Missing time = midday UTC (still sorts correctly).
static time_t sessionTime(JsonVariantConst o) {
  int y, m, d, hh = 12, mm = 0, ss = 0;
  if (!parseDate(str(o["date"]), y, m, d)) return 0;
  std::string t = str(o["time"]);
  if (!t.empty()) sscanf(t.c_str(), "%d:%d:%d", &hh, &mm, &ss);
  return utcToEpoch(y, m, d, hh, mm, ss);
}

std::string utf8Upper(const std::string& s) {
  std::string o; o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 'a' && c <= 'z') o += (char)(c - 32);
    else if (c == 0xC3 && i + 1 < s.size()) {    // Latin-1 letters: é -> É, ü -> Ü ...
      unsigned char n = (unsigned char)s[i + 1];
      if (n >= 0xA0 && n <= 0xBE && n != 0xB7) n -= 0x20;
      o += (char)c; o += (char)n; i++;
    } else o += (char)c;
  }
  return o;
}

static std::string replaceAll(std::string s, const char* a, const char* b) {
  size_t p, la = strlen(a), lb = strlen(b);
  for (size_t from = 0; (p = s.find(a, from)) != std::string::npos; from = p + lb) s.replace(p, la, b);
  return s;
}

// ---------------------------------------------------------------------
bool parseSchedule(JsonVariantConst root, AppData& d) {
  JsonArrayConst evs = root["events"];
  if (evs.isNull()) return false;
  std::vector<PbEvent> out;
  for (JsonVariantConst e : evs) {
    int y1, m1, d1, y2, m2, d2;
    if (!parseDate(str(e["start"]), y1, m1, d1) || !parseDate(str(e["end"]), y2, m2, d2)) continue;
    PbEvent p;
    p.tour = str(e["tour"]); p.name = str(e["name"]); p.city = str(e["city"]);
    p.start = ukMidnight(y1, m1, d1);
    p.end = ukMidnight(y2, m2, d2 + 1);           // mktime normalises day 32 etc.
    out.push_back(p);
  }
  std::sort(out.begin(), out.end(), [](const PbEvent& a, const PbEvent& b) { return a.start < b.start; });
  d.events = out;

  JsonObjectConst s = root["seeds"];
  d.seeds = Seeds();
  if (!s.isNull()) {
    d.seeds.event = str(s["event"]);
    d.seeds.ms = str(s["MS"]); d.seeds.ws = str(s["WS"]);
    d.seeds.md = str(s["MD"]); d.seeds.wd = str(s["WD"]);
    d.seeds.valid = !d.seeds.event.empty();
  }
  d.scheduleOk = true;
  return true;
}

bool parseRaces(JsonVariantConst root, AppData& d) {
  JsonArrayConst races = root["MRData"]["RaceTable"]["Races"];
  if (races.isNull()) return false;
  std::vector<Race> out;
  for (JsonVariantConst r : races) {
    Race rc;
    rc.round = atoi(str(r["round"]).c_str());
    rc.shortName = utf8Upper(replaceAll(str(r["raceName"]), "Grand Prix", "GP"));
    rc.locality = str(r["Circuit"]["Location"]["locality"]);
    if (!r["Sprint"].isNull())     rc.sessions.push_back({"SPRINT", sessionTime(r["Sprint"]), 60});
    if (!r["Qualifying"].isNull()) rc.sessions.push_back({"QUALI", sessionTime(r["Qualifying"]), 60});
    rc.sessions.push_back({"RACE", sessionTime(r), 120});
    std::sort(rc.sessions.begin(), rc.sessions.end(), [](const Session& a, const Session& b) { return a.start < b.start; });
    out.push_back(rc);
  }
  d.races = out;
  d.racesOk = true;
  return true;
}

static std::string teamName(const std::string& id, const std::string& raw) {
  if (id == "rb") return "Racing Bulls";
  std::string n = replaceAll(raw, " F1 Team", "");
  return n;
}

bool parseDriverStandings(JsonVariantConst root, AppData& d) {
  JsonVariantConst list = root["MRData"]["StandingsTable"]["StandingsLists"][0];
  JsonArrayConst rows = list["DriverStandings"];
  if (rows.isNull()) return false;
  std::vector<DriverRow> out;
  for (JsonVariantConst r : rows) {
    DriverRow dr;
    dr.pos = atoi(str(r["position"]).c_str());
    dr.points = num(r["points"]);
    dr.first = str(r["Driver"]["givenName"]);
    dr.last = str(r["Driver"]["familyName"]);
    dr.code = str(r["Driver"]["code"]);
    if (dr.code.empty()) dr.code = utf8Upper(dr.last.substr(0, 3));
    dr.teamId = str(r["Constructors"][0]["constructorId"]);
    dr.teamName = teamName(dr.teamId, str(r["Constructors"][0]["name"]));
    if (dr.pos == 0) dr.pos = (int)out.size() + 1;   // "positionText":"-" for some
    out.push_back(dr);
  }
  d.drivers = out;
  d.standingsRound = atoi(str(list["round"]).c_str());
  // fill driver codes on teams if the team table is already there
  for (auto& t : d.teams) {
    t.codes.clear(); int n = 0;
    for (auto& dr : d.drivers) if (dr.teamId == t.id && n < 2) { if (n++) t.codes += "  \xC2\xB7  "; t.codes += dr.code; }
  }
  d.standingsOk = !d.drivers.empty();
  return true;
}

bool parseConstructorStandings(JsonVariantConst root, AppData& d) {
  JsonArrayConst rows = root["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"];
  if (rows.isNull()) return false;
  std::vector<TeamRow> out;
  for (JsonVariantConst r : rows) {
    TeamRow t;
    t.pos = atoi(str(r["position"]).c_str());
    t.points = num(r["points"]);
    t.id = str(r["Constructor"]["constructorId"]);
    t.name = teamName(t.id, str(r["Constructor"]["name"]));
    // only the top two drivers by points (teams can run 3+ over a season)
    int n = 0;
    for (auto& dr : d.drivers) if (dr.teamId == t.id && n < 2) { if (n++) t.codes += "  \xC2\xB7  "; t.codes += dr.code; }
    out.push_back(t);
  }
  d.teams = out;
  return true;
}

bool parseLastResults(JsonVariantConst root, AppData& d) {
  JsonArrayConst rows = root["MRData"]["RaceTable"]["Races"][0]["Results"];
  if (rows.isNull()) return false;
  d.lastResults.clear();
  for (JsonVariantConst r : rows) {
    LastResult lr;
    lr.code = str(r["Driver"]["code"]);
    lr.pos = atoi(str(r["position"]).c_str());
    d.lastResults.push_back(lr);
  }
  return true;
}

uint32_t teamColour(const std::string& id) {
  struct { const char* id; uint32_t c; } map[] = {
    {"mercedes", 0x27F4D2}, {"ferrari", 0xE8002D}, {"red_bull", 0x3671C6}, {"mclaren", 0xFF8000},
    {"rb", 0x6692FF}, {"alpine", 0xFF87BC}, {"haas", 0xDADDE0}, {"audi", 0xA3A9B0},
    {"sauber", 0x52E252}, {"williams", 0x64C4FF}, {"aston_martin", 0x229971}, {"cadillac", 0xD4B26A},
  };
  for (auto& m : map) if (id == m.id) return m.c;
  return 0x8B95A5;
}
