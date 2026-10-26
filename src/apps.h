#pragma once
#include <Arduino.h>
#include <vector>

// Firmware installed next to SmartBoot. Each app is an OTA app partition (its label is
// the app's key) plus the data partitions it asked for. Apps that ask for the same data
// partition name share it, like in M5Launcher. The registry lives in SmartBoot's own
// NVS, so installed apps can't wipe it.
struct App {
    String label;              // partition label, e.g. "doomfo"
    String id;                 // LauncherHub firmware ID
    String name, version;
    std::vector<String> data;  // labels of the data partitions it uses
    std::vector<String> grow;  // ...of those, storage that may take all free space (see installer)
    uint32_t size = 0;         // app partition size
    uint32_t color = 0;        // 0xRRGGBB tile color
};

namespace apps {

// Loads the installed apps and points the bootloader back at SmartBoot, so SmartBoot's
// own restarts (and the serial port's resets) don't start an app.
void begin();
void reload();

int count();
const App& get(int i);
const App* byId(const String& id);

void save(const App& app);           // registry entry; the installer writes partitions
void forget(const String& label);    // drop a registry entry

bool launch(const App& app, String* error = nullptr);  // restarts into the app
// Frees its partitions. When one app is left, its growable storage takes the free space.
bool remove(const App& app, String* error = nullptr);

}  // namespace apps
