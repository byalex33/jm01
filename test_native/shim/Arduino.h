// Minimal stand-in for Arduino.h so ui.cpp / parse.cpp build on a PC.
#pragma once
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#ifndef PI
#define PI 3.14159265358979f
#endif
uint32_t millis();
