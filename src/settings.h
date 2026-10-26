#pragma once
#include <Arduino.h>

struct Settings {
    uint8_t brightness = 3;   // level 1..5
    bool sound = true;
    uint8_t accent = 0;       // index into ACCENTS
    uint8_t dimAfter = 1;     // index into DIM_OPTIONS
    bool welcomed = false;    // first-run guide already shown
};

struct DimOption { const char* label; uint32_t ms; };
constexpr DimOption DIM_OPTIONS[] = {{"30 sec", 30000}, {"1 min", 60000}, {"5 min", 300000}, {"Never", 0}};
constexpr int DIM_OPTION_COUNT = sizeof(DIM_OPTIONS) / sizeof(DIM_OPTIONS[0]);
constexpr int BRIGHTNESS_LEVELS = 5;

namespace settings {
Settings& get();
void load();
void save();
void reset();
void applyBrightness();      // pushes the current level to the backlight
uint8_t backlightValue();    // 0..255 for the current level
}
