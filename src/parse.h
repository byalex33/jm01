// JSON -> model. Pure functions so they can be checked against saved responses.
#pragma once
#include <ArduinoJson.h>
#include "model.h"

// schedule.json from GitHub (events + seeds)
bool parseSchedule(JsonVariantConst root, AppData& d);
// Jolpica: /current.json, /current/driverStandings.json,
//          /current/constructorStandings.json, /current/last/results.json
bool parseRaces(JsonVariantConst root, AppData& d);
bool parseDriverStandings(JsonVariantConst root, AppData& d);
bool parseConstructorStandings(JsonVariantConst root, AppData& d);
bool parseLastResults(JsonVariantConst root, AppData& d);

// helpers shared with the UI
time_t ukMidnight(int y, int m, int d);          // needs TZ set (setenv/tzset)
time_t utcToEpoch(int y, int mo, int d, int h, int mi, int s);
std::string utf8Upper(const std::string& s);     // ASCII + Latin-1 letters
uint32_t teamColour(const std::string& constructorId);
