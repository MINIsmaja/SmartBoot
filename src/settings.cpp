#include "settings.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <nvs_flash.h>
#include "config.h"
#include "theme.h"

namespace {
Settings current;
Preferences prefs;
constexpr uint8_t BACKLIGHT[BRIGHTNESS_LEVELS] = {30, 70, 120, 180, 255};

// Can SmartBoot's NVS partition store a value? (Writing an unchanged value is free.)
bool privateNvsWritable() {
    nvs_handle_t h;
    if (nvs_open_from_partition(SB_NVS, "sb", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, "ok", 1);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// Every page of SmartBoot's NVS partition must be an NVS page (header checksum OK) or
// fully erased. After a fresh flash it may still hold an earlier firmware's data: NVS
// skips such pages forever, and once its few good pages fill up every write fails.
bool privateNvsClean() {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, SB_NVS);
    if (!part) return false;
    uint8_t buf[256];
    for (uint32_t page = 0; page < part->size; page += 4096) {
        if (esp_partition_read(part, page, buf, 32) != ESP_OK) return false;
        uint32_t state, crc;
        memcpy(&state, buf, 4);
        memcpy(&crc, buf + 28, 4);
        if (state != 0xFFFFFFFF) {
            if (esp_rom_crc32_le(0xFFFFFFFF, buf + 4, 24) != crc) return false;
            continue;
        }
        for (uint32_t at = 0; at < 4096; at += sizeof(buf)) {  // an empty page must be all 0xFF
            if (esp_partition_read(part, page + at, buf, sizeof(buf)) != ESP_OK) return false;
            for (uint8_t b : buf)
                if (b != 0xFF) return false;
        }
    }
    return true;
}

// Start SmartBoot's NVS partition clean if it holds leftovers or can't store a value.
void initPrivateNvs() {
    if (privateNvsClean() && nvs_flash_init_partition(SB_NVS) == ESP_OK && privateNvsWritable()) return;
    nvs_flash_erase_partition(SB_NVS);
    nvs_flash_init_partition(SB_NVS);
}

// Opens a namespace of the shared "nvs" partition for writing, only if it exists.
bool openShared(Preferences& p, const char* ns) {
    if (!p.begin(ns, true)) return false;
    p.end();
    return p.begin(ns, false);
}

// SmartBoot used to keep its settings and Wi-Fi in the shared "nvs" partition, where
// installed apps can read and wipe them. Move them to SmartBoot's own partition once
// and remove them from the shared one.
void migrateFromSharedNvs() {
    Preferences mine, old;
    if (mine.begin("smartboot", true, SB_NVS)) {  // already moved
        mine.end();
        return;
    }
    if (openShared(old, "net")) {
        String ssid = old.getString("ssid", ""), pass = old.getString("pass", "");
        if (ssid.length() && mine.begin("net", false, SB_NVS)) {
            mine.putString("ssid", ssid);
            mine.putString("pass", pass);
            mine.end();
        }
        old.clear();
        old.end();
    }
    mine.begin("smartboot", false, SB_NVS);  // creating the namespace marks the move as done
    if (openShared(old, "smartboot")) {
        mine.putUChar("bright", old.getUChar("bright", 3));
        mine.putBool("sound", old.getBool("sound", true));
        mine.putUChar("accent", old.getUChar("accent", 0));
        mine.putUChar("dim", old.getUChar("dim", 1));
        mine.putBool("welcomed", old.getBool("welcomed", false));
        old.clear();
        old.end();
    }
    mine.end();
    for (const char* ns : {"catalog", "ota"}) {  // old catalog URL and demo install record
        if (openShared(old, ns)) {
            old.clear();
            old.end();
        }
    }
}
}  // namespace

uint16_t accent() { return ACCENTS[current.accent % ACCENT_COUNT].color; }

namespace settings {

Settings& get() { return current; }

void load() {
    initPrivateNvs();
    migrateFromSharedNvs();
    prefs.begin("smartboot", false, SB_NVS);  // read-write creates the namespace on first boot
    current.brightness = constrain(prefs.getUChar("bright", 3), 1, BRIGHTNESS_LEVELS);
    current.sound = prefs.getBool("sound", true);
    current.accent = prefs.getUChar("accent", 0) % ACCENT_COUNT;
    current.dimAfter = prefs.getUChar("dim", 1) % DIM_OPTION_COUNT;
    current.welcomed = prefs.getBool("welcomed", false);
    prefs.end();
}

void save() {
    prefs.begin("smartboot", false, SB_NVS);
    prefs.putUChar("bright", current.brightness);
    prefs.putBool("sound", current.sound);
    prefs.putUChar("accent", current.accent);
    prefs.putUChar("dim", current.dimAfter);
    prefs.putBool("welcomed", current.welcomed);
    prefs.end();
}

void reset() {
    bool welcomed = current.welcomed;
    current = Settings{};
    current.welcomed = welcomed;
    save();
    applyBrightness();
}

uint8_t backlightValue() { return BACKLIGHT[current.brightness - 1]; }

void applyBrightness() { M5Cardputer.Display.setBrightness(backlightValue()); }

}  // namespace settings
