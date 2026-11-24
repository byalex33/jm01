// GC9A01 panel (LovyanGFX) + LVGL. The faces draw into one 240x240 canvas.
#pragma once
#include <lvgl.h>

void displayBegin();                 // panel, backlight, LVGL, canvas
void displayBrightness(uint8_t v);   // 0-255
lv_obj_t* displayCanvas();
void displayLoop();                  // call often: runs LVGL
