// The watch faces. Same layout and coordinates as the browser preview.
#pragma once
#include <cstdint>
#include <string>
#include "model.h"

enum class Mode { Faces, Setup, Updating };

struct UiState {
  Mode mode = Mode::Faces;
  std::string driver = "VER";      // the driver James follows (3-letter code)
  // over-the-air update progress
  int otaPct = 0;
  int otaPhase = 0;                // 0 downloading, 1 installing, 2 restarting
  std::string otaFrom, otaTo;
  bool online = false;
};

void uiBegin(AppData* data, UiState* state);
void uiLoop();                       // redraws about 10 times a second, rotates faces
void uiRedrawNow();                  // force a frame (used during updates)
void uiNextFace();
void uiSetFace(int i);               // used by the PC preview in test_native/
int uiFaceCount();
void uiDrawBoot(uint32_t elapsedMs); // one frame of the startup splash
uint32_t uiBootMs();                 // total splash duration
