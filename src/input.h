#pragma once
#include <Arduino.h>

// The Cardputer has no real arrow keys: ; . , / carry the arrow symbols.
enum class Key : uint8_t { None, Up, Down, Left, Right, Ok, Back, Tab, Char };

struct KeyEvent {
    Key key = Key::None;
    char ch = 0;  // the typed character for Key::Char
};

namespace input {
// Polls the keyboard (with auto-repeat for held arrows) and returns one event per call.
KeyEvent poll();
// Queues a key as if it were typed; used by the serial dev tools.
void inject(Key key, char ch = 0);
// Text mode: ; . , / space return as typed characters instead of arrows/OK.
void setTextMode(bool on);
bool getTextMode();
uint32_t lastActivityMs();
}
