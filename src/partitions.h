#pragma once
#include <Arduino.h>
#include <vector>

// The flash partition table at 0x8000: read it, change it in RAM, write it back.
// Ported from M5Launcher's partition_table_model (https://github.com/bmorcelli/Launcher,
// MIT License, Copyright (c) 2023 shikarunochi; see licenses/M5Launcher-MIT.txt),
// trimmed to what SmartBoot needs.
//
// Installed apps are OTA app partitions (ota_0, ota_1, ...) placed in free space. The
// bootloader boots OTA slot (otadata seq - 1) % count, so OTA subtypes must stay
// contiguous; renumberOta() keeps them that way after a removal.
namespace parts {

constexpr uint32_t TABLE_OFFSET = 0x8000;
constexpr uint32_t TABLE_SIZE = 0x1000;
constexpr uint32_t SECTOR = 0x1000;
constexpr uint32_t APP_ALIGN = 0x10000;

constexpr uint8_t TYPE_APP = 0x00, TYPE_DATA = 0x01;
constexpr uint8_t SUB_FACTORY = 0x00, SUB_OTA_0 = 0x10, SUB_TEST = 0x20;
constexpr uint8_t SUB_OTADATA = 0x00, SUB_NVS = 0x02;
constexpr uint8_t SUB_FAT = 0x81, SUB_SPIFFS = 0x82, SUB_LITTLEFS = 0x83;

struct Entry {
    uint8_t type = 0xFF;
    uint8_t subtype = 0xFF;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t flags = 0;
    char label[17] = {0};

    bool isApp() const { return type == TYPE_APP; }
    bool isData() const { return type == TYPE_DATA; }
    bool isOtaApp() const { return isApp() && subtype >= SUB_OTA_0 && subtype < SUB_TEST; }
    uint32_t end() const { return offset + size; }
};

struct Table {
    std::vector<Entry> entries;  // sorted by offset
    uint32_t flashSize = 0;
};

bool read(Table& table, String* error = nullptr);
bool validate(const Table& table, String* error = nullptr);
// Writes the table (with its MD5 entry) and reads it back to verify. The cached
// esp_partition list of the running SmartBoot is NOT updated; SmartBoot only touches
// app and data partitions through raw flash offsets.
bool write(const Table& table, String* error = nullptr);

Entry* find(Table& table, const char* label);
const Entry* find(const Table& table, const char* label);

// Adds an OTA app partition big enough for imageSize, in the smallest free gap that fits.
bool addApp(Table& table, uint32_t imageSize, const char* label, Entry* created, String* error = nullptr);
bool addData(Table& table, uint8_t subtype, const char* label, uint32_t size, Entry* created,
             String* error = nullptr);
bool remove(Table& table, const char* label);
void renumberOta(Table& table);
uint32_t largestFree(const Table& table);

// otadata: make the bootloader start this OTA app, or (clear) the factory app.
bool setBoot(const Table& table, const Entry& app, String* error = nullptr);
bool clearBoot(const Table& table, String* error = nullptr);
bool bootSelectsApp(const Table& table);

}  // namespace parts
