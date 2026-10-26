// SmartBoot - a friendly firmware launcher for the M5Stack Cardputer ADV.
// Step 1: the user interface. The store uses built-in sample data and installs are simulated.
#include <M5Cardputer.h>
#include "catalog.h"
#include "devtools.h"
#include "input.h"
#include "net.h"
#include "apps.h"
#include "screens.h"
#include "settings.h"
#include "ui.h"

// Downloads (TLS) run on the loop task, and installs draw their progress from inside the
// download; with the default 8 KB only ~2 KB was left during an install.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

namespace {
constexpr uint32_t FRAME_MS = 16;
bool dimmed = false;
bool toastShown = false;
uint32_t lastFrame = 0;
uint32_t lastRefresh = 0;

void click(Key k) {
    if (settings::get().sound) M5Cardputer.Speaker.tone(k == Key::Back ? 1400 : 2200, 12);
}

void dimWhenIdle() {
    uint32_t after = DIM_OPTIONS[settings::get().dimAfter].ms;
    if (!dimmed && after && millis() - input::lastActivityMs() > after) {
        M5Cardputer.Display.setBrightness(8);
        dimmed = true;
    }
}
}  // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    Serial.setRxBufferSize(8192);  // room for the catalog's large header line on sideload
    settings::load();
    devtools::begin();

    M5Cardputer.Display.setRotation(1);
    settings::applyBrightness();
    M5Cardputer.Speaker.setVolume(80);
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    catalog::begin();  // mount storage, load cached/built-in list
    net::begin();      // start Wi-Fi, reconnect to a saved network
    apps::begin();     // installed apps; boot points back at SmartBoot

    nav::reset(screens::splash());
    nav::render();
}

void loop() {
    M5Cardputer.update();
#ifndef SMARTBOOT_RELEASE
    devtools::poll();  // serial dev console (screenshots, test installs); not in release builds
#endif
    net::poll();

    bool redraw = false;
    KeyEvent e = input::poll();
    if (e.key != Key::None) {
        if (dimmed) {  // the first key only wakes the screen
            dimmed = false;
            settings::applyBrightness();
        } else {
            click(e.key);
            nav::top()->onKey(e);
        }
        redraw = true;
    }
    if (nav::top()->update()) redraw = true;
    if (ui::toastVisible() != toastShown) {
        toastShown = !toastShown;
        redraw = true;
    }
    if (millis() - lastRefresh > 5000) {  // keeps battery and status info fresh
        lastRefresh = millis();
        redraw = true;
    }
    if (redraw) nav::render();
    dimWhenIdle();

    uint32_t elapsed = millis() - lastFrame;
    if (elapsed < FRAME_MS) delay(FRAME_MS - elapsed);
    lastFrame = millis();
}
