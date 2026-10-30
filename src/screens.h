#pragma once
#include "catalog.h"
#include "installer.h"
#include "input.h"
#include "ui.h"

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() {}
    virtual void onKey(const KeyEvent& e) = 0;
    virtual bool update() { return false; }  // advance animations; true = redraw needed
    virtual void draw() = 0;
    virtual bool isOverlay() const { return false; }  // dialogs draw over the screen below
};

namespace nav {
void push(Screen* s);
void pop();
void reset(Screen* s);  // clear history and show s
Screen* top();
void render();
}

namespace screens {
Screen* splash();
Screen* welcome();
Screen* home();
Screen* store();
Screen* details(const Firmware& fw, const installer::Check& check);
Screen* confirmInstall(const Firmware& fw, const installer::Check& check);
Screen* install(const Firmware& fw, const installer::Check& check);
Screen* confirmRemove(const App& app);
Screen* sdBrowser();  // install .bin files from the microSD card
Screen* transfer();   // get files onto the SD card: Wi-Fi upload page or USB drive
Screen* wifiShare();  // the Wi-Fi upload page
Screen* usbDrive();   // the SD card as a USB drive
Screen* usbDrivePreview();  // dev console: its running screen without starting USB
Screen* settings();
Screen* about();
Screen* comingSoon(const char* title, ui::Icon icon, uint16_t color, const char* message);

// Wi-Fi + catalog screens
Screen* wifi();
Screen* wifiPassword(const String& ssid, bool open);
Screen* updating(bool backToStore);  // downloads the catalog with a progress bar
}
