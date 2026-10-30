#include "input.h"
#include <M5Cardputer.h>

namespace {
constexpr uint32_t REPEAT_DELAY_MS = 380;
constexpr uint32_t REPEAT_RATE_MS  = 85;

KeyEvent injected;
KeyEvent held;
uint32_t heldSince = 0;
uint32_t lastRepeat = 0;
uint32_t lastActivity = 0;
bool textMode = false;  // when set, keys return raw characters for typing

KeyEvent fromChar(char c) {
    switch (c) {
        case ';': return {Key::Up};
        case '.': return {Key::Down};
        case ',': return {Key::Left};
        case '/': return {Key::Right};
        case '`': return {Key::Back};  // the "esc" key
        case ' ': return {Key::Ok};
        default:  return {Key::Char, c};
    }
}

KeyEvent currentlyHeld() {
    if (!M5Cardputer.Keyboard.isPressed()) return {};
    auto state = M5Cardputer.Keyboard.keysState();
    if (state.enter) return {Key::Ok};
    if (state.del) return {Key::Back};
    if (state.tab) return {Key::Tab};
    if (!state.word.empty()) {
        char c = state.word[0];
        // Text mode returns every printable key (space and punctuation included)
        // as a character; navigation mode remaps ; . , / to the arrows.
        return textMode ? KeyEvent{Key::Char, c} : fromChar(c);
    }
    if (!textMode && state.space) return {Key::Ok};
    return {};
}

bool repeats(Key k) {
    return k == Key::Up || k == Key::Down || k == Key::Left || k == Key::Right;
}
}  // namespace

namespace input {

KeyEvent poll() {
    uint32_t now = millis();
    if (injected.key != Key::None) {
        KeyEvent e = injected;
        injected = {};
        lastActivity = now;
        return e;
    }

    KeyEvent k = currentlyHeld();
    if (k.key != held.key || k.ch != held.ch) {
        held = k;
        heldSince = lastRepeat = now;
        if (k.key != Key::None) lastActivity = now;
        return k;
    }
    if (repeats(k.key) && now - heldSince > REPEAT_DELAY_MS && now - lastRepeat > REPEAT_RATE_MS) {
        lastRepeat = lastActivity = now;
        return k;
    }
    return {};
}

void inject(Key key, char ch) { injected = {key, ch}; }

void setTextMode(bool on) { textMode = on; }
bool getTextMode() { return textMode; }

uint32_t lastActivityMs() { return lastActivity; }

void touch() { lastActivity = millis(); }

}  // namespace input
