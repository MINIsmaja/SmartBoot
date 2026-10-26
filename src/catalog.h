#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

// Firmware comes from LauncherHub, M5Launcher's online catalog. Its firmware IDs are
// M5Burner's, and its downloads redirect to M5Burner's CDN.
#define HUB_API      "https://api.launcherhub.net"
#define FIRMWARE_CDN "https://m5burner-cdn.m5stack.com/firmware/"

// Full record, used for the Recommended tab and the open Details page. Version, sizes
// and the download link are only known after fetchDetails().
struct Firmware {
    String id, name, author, tagline, about, good, url, version, date;
    uint32_t size = 0;        // full flash image size in bytes (for display)
    uint32_t appSize = 0;     // app image bytes to write to the OTA slot
    uint32_t appOffset = 0;   // bytes to skip at the start of the download
    uint32_t color = 0;       // 0xRRGGBB tile color, derived from the name
    bool recommended = false;
};

// Lean index entry kept in RAM for every "All" firmware (~68 bytes). The firmware ID
// lives on flash and is read when a details page opens.
struct FwCard {
    uint32_t off = 0;         // byte offset of this entry's line in the cache file
    uint32_t color = 0;
    char name[40] = {0};
    char author[20] = {0};
};

namespace catalog {

enum class Source : uint8_t { None, BuiltIn, Cache, Network };

void begin();  // mount storage, load cached catalog or the built-in fallback

int recommendedCount();
Firmware recommended(int i);  // built on demand from the table in flash (recommended.h)
uint32_t tileColor(const char* name);  // a calm 0xRRGGBB color picked from the name

int allCount();
const FwCard& all(int i);        // most downloaded first
Firmware loadDetails(const FwCard& c);  // ID, name and blurbs from flash (works offline)
// Fill in version, sizes and the download link from LauncherHub. Needs Wi-Fi.
bool fetchDetails(Firmware& fw, String* error = nullptr);

// Search over the All list.
void setQuery(const String& q);
const String& query();
int filteredCount();
const FwCard& filtered(int i);

Source source();
const String& updated();
bool checkedOnline();

// Download a fresh "All" list over Wi-Fi and cache it. onProgress may be null.
struct RefreshResult { bool ok; bool updated; String message; };
RefreshResult refresh(const std::function<void(int)>& onProgress = nullptr);

// Sideload an "All" list over USB serial (cache NDJSON format), no Wi-Fi needed.
void importBegin();
void importLine(const String& line, size_t index);
void importEnd();

}  // namespace catalog
