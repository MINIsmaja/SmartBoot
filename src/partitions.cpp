// Ported from M5Launcher's partition_table_model.cpp (https://github.com/bmorcelli/Launcher),
// MIT License, Copyright (c) 2023 shikarunochi; see licenses/M5Launcher-MIT.txt.
#include "partitions.h"
#include <algorithm>
#include <memory>
#include <bootloader_common.h>
#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <mbedtls/md5.h>
#include "config.h"

namespace {
using parts::Entry;
using parts::Table;

// Partitions SmartBoot itself depends on; a table without them is never written.
const char* const KEEP[] = {"nvs", "otadata", "factory", SB_NVS, SB_FS};

void setError(String* error, const char* message) {
    if (error) *error = message;
}

uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void writeU32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

void sortByOffset(Table& t) {
    std::sort(t.entries.begin(), t.entries.end(),
              [](const Entry& a, const Entry& b) { return a.offset < b.offset; });
}

uint8_t countOta(const Table& t) {
    uint8_t n = 0;
    for (const Entry& e : t.entries) n += e.isOtaApp();
    return n;
}

// Smallest free gap that holds `size` bytes at `align`; false if none.
bool bestFit(const Table& t, uint32_t size, uint32_t align, uint32_t& offset) {
    uint32_t bestWaste = UINT32_MAX;
    uint32_t cursor = parts::TABLE_OFFSET + parts::TABLE_SIZE;
    auto consider = [&](uint32_t start, uint32_t end) {
        uint32_t at = alignUp(start, align);
        if (at >= end || end - at < size) return;
        uint32_t waste = (end - start) - size;
        if (waste < bestWaste) {
            bestWaste = waste;
            offset = at;
        }
    };
    for (const Entry& e : t.entries) {  // entries are sorted
        if (e.offset > cursor) consider(cursor, e.offset);
        cursor = std::max(cursor, e.end());
    }
    if (t.flashSize > cursor) consider(cursor, t.flashSize);
    return bestWaste != UINT32_MAX;
}

// Raw otadata sectors (two copies, the newer valid one wins).
const Entry* otadata(const Table& t) {
    for (const Entry& e : t.entries)
        if (e.isData() && e.subtype == parts::SUB_OTADATA) return &e;
    return nullptr;
}

bool readOtadata(const Entry& od, esp_ota_select_entry_t (&s)[2]) {
    return esp_flash_read(nullptr, &s[0], od.offset, sizeof(s[0])) == ESP_OK &&
           esp_flash_read(nullptr, &s[1], od.offset + parts::SECTOR, sizeof(s[1])) == ESP_OK;
}
}  // namespace

namespace parts {

bool read(Table& table, String* error) {
    table = Table();
    if (esp_flash_get_size(nullptr, &table.flashSize) != ESP_OK || !table.flashSize) {
        setError(error, "Could not detect flash size");
        return false;
    }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[TABLE_SIZE]);
    if (!buf) {
        setError(error, "Out of memory");
        return false;
    }
    if (esp_flash_read(nullptr, buf.get(), TABLE_OFFSET, TABLE_SIZE) != ESP_OK) {
        setError(error, "Could not read the partition table");
        return false;
    }
    for (uint32_t pos = 0; pos + 32 <= TABLE_SIZE; pos += 32) {
        const uint8_t* p = buf.get() + pos;
        uint16_t magic = p[0] | (p[1] << 8);
        if (magic == ESP_PARTITION_MAGIC_MD5) {
            uint8_t md5[16];
            if (mbedtls_md5(buf.get(), pos, md5) != 0 || memcmp(md5, p + ESP_PARTITION_MD5_OFFSET, 16)) {
                setError(error, "Partition table checksum is wrong");
                return false;
            }
            break;
        }
        if (magic == 0xFFFF) break;
        if (magic != ESP_PARTITION_MAGIC) {
            setError(error, "Partition table is damaged");
            return false;
        }
        Entry e;
        e.type = p[2];
        e.subtype = p[3];
        e.offset = readU32(p + 4);
        e.size = readU32(p + 8);
        memcpy(e.label, p + 12, 16);
        e.flags = readU32(p + 28);
        table.entries.push_back(e);
    }
    sortByOffset(table);
    return validate(table, error);
}

bool validate(const Table& table, String* error) {
    if ((table.entries.size() + 1) * 32 > ESP_PARTITION_TABLE_MAX_LEN) {
        setError(error, "Too many partitions");
        return false;
    }
    for (const char* label : KEEP) {
        if (!find(table, label)) {
            setError(error, "A SmartBoot partition is missing");
            return false;
        }
    }
    uint32_t otaSeen = 0;
    for (size_t i = 0; i < table.entries.size(); i++) {
        const Entry& e = table.entries[i];
        if (!e.size || e.offset % SECTOR || e.size % SECTOR) {
            setError(error, "Partition is not sector aligned");
            return false;
        }
        if (e.offset < TABLE_OFFSET + TABLE_SIZE || e.end() > table.flashSize || e.end() < e.offset) {
            setError(error, "Partition is outside the usable flash");
            return false;
        }
        if (!e.label[0]) {
            setError(error, "Partition has no name");
            return false;
        }
        if (e.isApp() && e.offset % APP_ALIGN) {
            setError(error, "App partition is not 64 KB aligned");
            return false;
        }
        if (e.isOtaApp()) otaSeen |= 1u << (e.subtype - SUB_OTA_0);
        for (size_t j = i + 1; j < table.entries.size(); j++) {
            const Entry& o = table.entries[j];
            if (e.offset < o.end() && o.offset < e.end()) {
                setError(error, "Partitions overlap");
                return false;
            }
            if (!strncmp(e.label, o.label, 16)) {
                setError(error, "Two partitions have the same name");
                return false;
            }
        }
    }
    if (otaSeen & (otaSeen + 1)) {  // must be ota_0..ota_n-1 without gaps
        setError(error, "App slots are not numbered in order");
        return false;
    }
    return true;
}

bool write(const Table& table, String* error) {
    if (!validate(table, error)) return false;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[TABLE_SIZE]);
    std::unique_ptr<uint8_t[]> check(new (std::nothrow) uint8_t[TABLE_SIZE]);
    if (!buf || !check) {
        setError(error, "Out of memory");
        return false;
    }
    memset(buf.get(), 0xFF, TABLE_SIZE);
    uint32_t pos = 0;
    for (const Entry& e : table.entries) {
        uint8_t* p = buf.get() + pos;
        p[0] = ESP_PARTITION_MAGIC & 0xFF;
        p[1] = ESP_PARTITION_MAGIC >> 8;
        p[2] = e.type;
        p[3] = e.subtype;
        writeU32(p + 4, e.offset);
        writeU32(p + 8, e.size);
        memset(p + 12, 0, 16);
        strncpy((char*)p + 12, e.label, 15);
        writeU32(p + 28, e.flags);
        pos += 32;
    }
    uint8_t* md5 = buf.get() + pos;
    md5[0] = ESP_PARTITION_MAGIC_MD5 & 0xFF;
    md5[1] = ESP_PARTITION_MAGIC_MD5 >> 8;
    if (mbedtls_md5(buf.get(), pos, md5 + ESP_PARTITION_MD5_OFFSET) != 0) {
        setError(error, "Could not checksum the partition table");
        return false;
    }

    // IDF aborts on writes below 0x9000 (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS). Use a
    // copy of the main flash chip whose OS hooks skip only that check; locking, cache
    // handling and pausing the other core stay as they are. (Not esp_flash_app_disable_protect():
    // it switches to the panic handler's no-OS hooks, and the other core then crashes while
    // running from flash. That reset SmartBoot mid-write once and left the table blank.)
    static esp_flash_os_functions_t hooks;
    static esp_flash_t chip;
    hooks = *esp_flash_default_chip->os_func;
    hooks.region_protected = nullptr;
    chip = *esp_flash_default_chip;
    chip.os_func = &hooks;
    for (int attempt = 0; attempt < 3; attempt++) {
        esp_err_t err = esp_flash_erase_region(&chip, TABLE_OFFSET, TABLE_SIZE);
        if (err == ESP_OK) err = esp_flash_write(&chip, buf.get(), TABLE_OFFSET, TABLE_SIZE);
        if (err == ESP_OK && esp_flash_read(nullptr, check.get(), TABLE_OFFSET, TABLE_SIZE) == ESP_OK &&
            !memcmp(buf.get(), check.get(), TABLE_SIZE))
            return true;
    }
    setError(error, "Could not write the partition table");
    return false;
}

Entry* find(Table& table, const char* label) {
    for (Entry& e : table.entries)
        if (!strncmp(e.label, label, 16)) return &e;
    return nullptr;
}

const Entry* find(const Table& table, const char* label) {
    for (const Entry& e : table.entries)
        if (!strncmp(e.label, label, 16)) return &e;
    return nullptr;
}

bool addApp(Table& table, uint32_t imageSize, const char* label, Entry* created, String* error) {
    Entry e;
    e.type = TYPE_APP;
    e.size = alignUp(imageSize, APP_ALIGN);
    strncpy(e.label, label, 15);
    if (countOta(table) >= 16) {
        setError(error, "No free app slot");
        return false;
    }
    if (!bestFit(table, e.size, APP_ALIGN, e.offset)) {
        setError(error, "Not enough free space");
        return false;
    }
    e.subtype = SUB_OTA_0 + countOta(table);
    table.entries.push_back(e);
    sortByOffset(table);
    renumberOta(table);
    if (created) *created = *find(table, label);
    return true;
}

bool addData(Table& table, uint8_t subtype, const char* label, uint32_t size, Entry* created, String* error) {
    Entry e;
    e.type = TYPE_DATA;
    e.subtype = subtype;
    e.size = alignUp(size, SECTOR);
    strncpy(e.label, label, 15);
    if (!bestFit(table, e.size, SECTOR, e.offset)) {
        setError(error, "Not enough free space");
        return false;
    }
    table.entries.push_back(e);
    sortByOffset(table);
    if (created) *created = e;
    return true;
}

bool remove(Table& table, const char* label) {
    for (auto it = table.entries.begin(); it != table.entries.end(); ++it) {
        if (!strncmp(it->label, label, 16)) {
            table.entries.erase(it);
            renumberOta(table);
            return true;
        }
    }
    return false;
}

void renumberOta(Table& table) {
    uint8_t next = SUB_OTA_0;
    for (Entry& e : table.entries)  // sorted by offset
        if (e.isOtaApp()) e.subtype = next++;
}

uint32_t largestFree(const Table& table) {
    uint32_t best = 0, cursor = TABLE_OFFSET + TABLE_SIZE;
    for (const Entry& e : table.entries) {
        if (e.offset > cursor) best = std::max(best, e.offset - cursor);
        cursor = std::max(cursor, e.end());
    }
    if (table.flashSize > cursor) best = std::max(best, table.flashSize - cursor);
    return best;
}

bool setBoot(const Table& table, const Entry& app, String* error) {
    const Entry* od = otadata(table);
    uint8_t count = countOta(table);
    if (!od || !app.isOtaApp() || !count) {
        setError(error, "Nothing to start");
        return false;
    }
    uint32_t index = app.subtype - SUB_OTA_0;
    esp_ota_select_entry_t s[2];
    memset(s, 0xFF, sizeof(s));
    readOtadata(*od, s);
    int active = bootloader_common_get_active_otadata(s);

    // The bootloader starts slot (seq - 1) % count from the copy with the highest seq,
    // so pick the next seq above the current one that lands on this slot.
    esp_ota_select_entry_t sel;
    memset(&sel, 0xFF, sizeof(sel));
    int target = 0;
    if (active < 0) {
        sel.ota_seq = index + 1;
    } else {
        uint32_t cur = s[active].ota_seq;
        sel.ota_seq = cur + 1 + (index + count - cur % count) % count;
        target = 1 - active;
    }
    sel.ota_state = ESP_OTA_IMG_UNDEFINED;  // like M5Launcher: no rollback bookkeeping
    sel.crc = bootloader_common_ota_select_crc(&sel);

    uint32_t at = od->offset + target * SECTOR;
    if (esp_flash_erase_region(nullptr, at, SECTOR) != ESP_OK ||
        esp_flash_write(nullptr, &sel, at, sizeof(sel)) != ESP_OK) {
        setError(error, "Could not set the boot app");
        return false;
    }
    return true;
}

bool clearBoot(const Table& table, String* error) {
    const Entry* od = otadata(table);
    if (!od || esp_flash_erase_region(nullptr, od->offset, od->size) != ESP_OK) {
        setError(error, "Could not reset the boot app");
        return false;
    }
    return true;
}

bool bootSelectsApp(const Table& table) {
    const Entry* od = otadata(table);
    esp_ota_select_entry_t s[2];
    return od && readOtadata(*od, s) && bootloader_common_get_active_otadata(s) >= 0;
}

}  // namespace parts
