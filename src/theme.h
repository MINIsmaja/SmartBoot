#pragma once
#include <M5GFX.h>

// 24-bit colors, converted to the panel's RGB565 at compile time.
constexpr uint16_t rgb(uint32_t c) {
    return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}

namespace col {
constexpr uint16_t bg       = rgb(0x0E1117);
constexpr uint16_t surface  = rgb(0x1A1F2B);
constexpr uint16_t surface2 = rgb(0x262C3B);
constexpr uint16_t line     = rgb(0x343B4D);
constexpr uint16_t text     = rgb(0xEDF0F7);
constexpr uint16_t muted    = rgb(0x8C95A8);
constexpr uint16_t faint    = rgb(0x596277);
constexpr uint16_t success  = rgb(0x3DD68C);
constexpr uint16_t warning  = rgb(0xFFB547);
constexpr uint16_t danger   = rgb(0xFF5C6C);
}

// User-selectable accent colors (Settings > Accent).
struct Accent { const char* name; uint16_t color; };
constexpr Accent ACCENTS[] = {
    {"Blue",   rgb(0x4F8CFF)},
    {"Purple", rgb(0x9B7BFF)},
    {"Green",  rgb(0x2FCB8A)},
    {"Orange", rgb(0xFF9A3D)},
    {"Pink",   rgb(0xFF6FAE)},
};
constexpr int ACCENT_COUNT = sizeof(ACCENTS) / sizeof(ACCENTS[0]);

uint16_t accent();  // current accent, from settings

// Mixes two RGB565 colors; t = 0 gives a, t = 255 gives b.
inline uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {
    int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
    int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
    int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
    return (r << 11) | (g << 5) | bl;
}

namespace font {
inline const lgfx::IFont* title() { return &fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* body()  { return &fonts::DejaVu12; }
inline const lgfx::IFont* small() { return &fonts::DejaVu9; }
inline const lgfx::IFont* big()   { return &fonts::FreeSansBold12pt7b; }
}
