// Data the faces show. Filled by the parsers in parse.cpp, no hardware code here.
#pragma once
#include <string>
#include <vector>
#include <ctime>

struct PbEvent {               // a pickleball event from schedule.json
  std::string tour;            // "PPA" or "MLP"
  std::string name, city;
  time_t start = 0;            // UK midnight of the first day
  time_t end = 0;              // UK midnight AFTER the last day (exclusive)
};

struct Seeds {                 // number one seeds at the next big event
  std::string event, ms, ws, md, wd;
  bool valid = false;
};

struct Session {
  std::string kind;            // "QUALI", "SPRINT", "RACE"
  time_t start = 0;
  int durMin = 60;
  Session() {}
  Session(const char* k, time_t s, int d) : kind(k), start(s), durMin(d) {}
};

struct Race {
  int round = 0;
  std::string shortName;       // "QATAR GP"
  std::string locality;        // "Lusail"
  std::vector<Session> sessions;   // in time order, race last
};

struct DriverRow {
  int pos = 0;
  std::string code, first, last, teamId, teamName;
  float points = 0;
};

struct TeamRow {
  int pos = 0;
  std::string id, name, codes; // codes: "RUS  ·  ANT"
  float points = 0;
};

struct LastResult { std::string code; int pos = 0; };

struct AppData {
  std::vector<PbEvent> events;  Seeds seeds;  bool scheduleOk = false;
  std::vector<Race> races;      bool racesOk = false;
  std::vector<DriverRow> drivers;
  std::vector<TeamRow> teams;   int standingsRound = 0;  bool standingsOk = false;
  std::vector<LastResult> lastResults;
};
