#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "apps.h"
#include "catalog.h"
#include "partitions.h"

// Installs LauncherHub firmware as an app next to SmartBoot, following M5Launcher's
// manifest-based install (https://github.com/bmorcelli/Launcher, MIT License):
//   1. fetchManifest(): where the app and its data partitions sit in the download
//   2. plan(): where they go in flash; which other apps must go if space is short
//   3. install(): download into free flash, verify, then write the new partition table.
// The table is written last, so a failed download leaves the device as it was (apart
// from apps the user already agreed to remove).
namespace installer {

struct DataPart {
    uint8_t subtype = 0;        // parts::SUB_FAT / SUB_SPIFFS / SUB_LITTLEFS
    String label;
    uint32_t size = 0;          // size in the firmware's own layout
    uint32_t srcOffset = 0;     // where its content starts in the download
    uint32_t copySize = 0;      // 0 = no content, the app formats it
};

struct Manifest {
    String version;
    uint32_t appOffset = 0;     // where the app image starts in the download
    uint32_t appSize = 0;
    std::vector<DataPart> data;
    String sdPath;              // install from this file on the SD card instead of downloading
};

// Needs fw.id, fw.version (from catalog::fetchDetails) and Wi-Fi.
bool fetchManifest(const Firmware& fw, Manifest& m, String* error = nullptr);

struct Fill {                   // a data partition to prepare
    parts::Entry part;
    bool erase = false;         // new partition: erase it so the app starts clean
    uint32_t srcOffset = 0, copySize = 0;
};

struct Plan {
    bool ok = false;
    String reason;              // why it can't be installed (when !ok)
    std::vector<App> removes;   // other apps that must be removed to make room
    std::vector<String> shrinks;  // "Game Station's storage shrinks to 1.2 MB"
    bool replaces = false;      // updates an older install of the same firmware
    App old;                    // ...which is this one
    parts::Table table;         // the table after installing
    parts::Table fallback;      // without the apps being removed; written before their space is reused
    bool fallbackNeeded = false;
    parts::Entry app;
    std::vector<Fill> fills;
    std::vector<String> dataLabels;
};

// Pure planning against the current partition table; writes nothing.
Plan plan(const Firmware& fw, const Manifest& m);

// What the store knows about installing one firmware: its manifest and the plan.
struct Check {
    bool done = false;          // false: offline, or details didn't load
    String error;               // why the manifest couldn't be read
    Manifest manifest;
    Plan plan;
};
Check check(const Firmware& fw);  // fetchManifest() + plan(); needs Wi-Fi

struct Result { bool ok; String message; };
// onProgress(stage, 0..100). Blocks until done (tens of seconds).
Result install(const Firmware& fw, const Manifest& m, const Plan& p,
               const std::function<void(const char*, int)>& onProgress = nullptr);

}  // namespace installer
