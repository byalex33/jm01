// Wi-Fi, data downloads and over-the-air updates.
#pragma once
#include "model.h"
#include "ui.h"

void netBegin(AppData* data, UiState* state);  // starts Wi-Fi (or the setup portal) and the download task
void netLoop();                                 // call from loop(): portal, settings page, health check

// The download task and the screen share AppData; hold the lock while reading or writing it.
void dataLock();
void dataUnlock();
