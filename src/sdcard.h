#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "catalog.h"
#include "installer.h"

// The microSD card: mount it, list folders and firmware files, and turn a .bin file into
// an install the store's installer runs (same plan, same Details and Install screens).
namespace sdcard {

bool mount();    // true when a card is ready (mounts on first use)
void unmount();  // frees the SPI bus and buffers when leaving the SD screens

struct Entry {
    String name;
    uint32_t size = 0;  // bytes; 0 for folders
    bool dir = false;
};

struct Listing {
    std::vector<Entry> entries;  // folders first, then A-Z
    uint16_t hidden = 0;         // files left out (not firmware, or hidden system files)
    bool truncated = false;      // the folder had more entries than we keep in RAM
};

// Lists a folder. Without allFiles only folders and .bin files are kept.
bool list(const String& path, bool allFiles, Listing& out);

// Reads a .bin file's own headers (full flash image or app only, which chip it was built
// for, app name and version) and plans its install. Fills fw for the Details screen.
installer::Check inspect(const String& path, Firmware& fw);

// Streams bytes [offset, offset + len) of a file to onChunk; false on a read error.
bool read(const String& path, uint32_t offset, uint32_t len,
          const std::function<bool(const uint8_t*, size_t)>& onChunk);

}  // namespace sdcard
