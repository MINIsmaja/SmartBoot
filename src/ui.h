#pragma once
#include <M5Cardputer.h>
#include <initializer_list>
#include "config.h"
#include "theme.h"

// Everything is drawn into this full-screen sprite, then pushed in one go (no flicker).
extern M5Canvas canvas;

namespace ui {

enum class Icon : uint8_t { Store, SdCard, Wifi, Gear, Info, Star, Check, Power, Transfer };
enum class Glyph : uint8_t { None, Up, Down, Left, Right, Enter };

// Footer hint keys: "ud", "lr", "ok", or literal key text such as "esc" / "tab".
struct Hint { const char* key; const char* action; };

void drawIcon(Icon icon, int cx, int cy, int size, uint16_t color, uint16_t bg);
void statusBar(const char* title);
void footer(std::initializer_list<Hint> hints);
// Draws a keycap and returns its width. label may be empty when only a glyph is wanted.
int keycap(int x, int y, int h, const char* label, Glyph g1 = Glyph::None, Glyph g2 = Glyph::None);

void glyph(Glyph g, int cx, int cy, uint16_t color);
void toggle(int x, int y, bool on);
void firmwareTile(int x, int y, int size, uint32_t rgb888, const char* name);
// Word-wraps text into a column and returns the height used.
int wrapText(const char* text, int x, int y, int w, int lineH, uint16_t color, bool center = false);
void progressBar(int x, int y, int w, int h, float t, uint16_t color);
void scrollbar(int x, int y, int h, float offset, float visible, float total);
void darken();  // dims the whole canvas behind a dialog

void toast(const char* msg, uint16_t color = col::text);
bool toastVisible();
void drawToast();

// Moves v toward target; returns true while still moving.
inline bool approach(float& v, float target, float k = 0.35f) {
    float d = target - v;
    if (fabsf(d) < 0.02f) { v = target; return false; }
    v += d * k;
    return true;
}

}  // namespace ui
