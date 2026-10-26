#include "sdcard.h"
#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <memory>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include "partitions.h"

namespace {
// The Cardputer's microSD slot (M5Cardputer's SD example), on its own SPI bus.
constexpr int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;
constexpr size_t MAX_ENTRIES = 256;  // per folder; keeps a huge folder from filling RAM
constexpr uint8_t IMAGE_MAGIC = 0xE9;
constexpr uint16_t CHIP_ESP32S3 = 9;

bool mounted = false;

bool endsWithBin(const String& name) {
    return name.length() > 4 && name.substring(name.length() - 4).equalsIgnoreCase(".bin");
}

String baseName(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash < 0 ? path : path.substring(slash + 1);
}

const char* chipName(uint16_t id) {
    switch (id) {
        case 0: return "ESP32";
        case 2: return "ESP32-S2";
        case 5: return "ESP32-C3";
        case 12: return "ESP32-C2";
        case 13: return "ESP32-C6";
        case 16: return "ESP32-H2";
        case 18: return "ESP32-P4";
        case 23: return "ESP32-C5";
        default: return "another chip";
    }
}

bool readAt(File& f, uint32_t offset, void* buf, size_t len) {
    return f.seek(offset) && f.read((uint8_t*)buf, len) == len;
}

// Length of the app image at `offset`: header, segments, checksum padding to 16 bytes,
// then an optional SHA-256. 0 if the file ends before the image does.
uint32_t imageLength(File& f, uint32_t offset, const esp_image_header_t& h) {
    uint32_t pos = offset + sizeof(esp_image_header_t);
    for (int i = 0; i < h.segment_count; i++) {
        esp_image_segment_header_t seg;
        if (!readAt(f, pos, &seg, sizeof(seg))) return 0;
        pos += sizeof(seg) + seg.data_len;
        if (pos > f.size()) return 0;
    }
    uint32_t len = ((pos - offset) + 1 + 15) & ~15u;
    if (h.hash_appended) len += 32;
    return offset + len <= f.size() ? len : 0;
}

void refuse(installer::Check& c, const String& reason) {
    c.done = true;
    c.plan = installer::Plan();
    c.plan.reason = reason;
}
}  // namespace

namespace sdcard {

bool mount() {
    if (mounted) return true;
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    mounted = SD.begin(PIN_CS, SPI, 25000000);
    if (!mounted) {
        SD.end();
        SPI.end();
    }
    return mounted;
}

void unmount() {
    if (!mounted) return;
    SD.end();
    SPI.end();
    mounted = false;
}

bool list(const String& path, bool allFiles, Listing& out) {
    out = Listing();
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return false;
    bool isDir = false;
    for (String full = dir.getNextFileName(&isDir); full.length(); full = dir.getNextFileName(&isDir)) {
        String name = baseName(full);
        bool keep = !name.startsWith(".") && name != "System Volume Information" &&
                    (isDir || allFiles || endsWithBin(name));
        if (!keep) {
            out.hidden++;
            continue;
        }
        if (out.entries.size() >= MAX_ENTRIES) {
            out.truncated = true;
            continue;
        }
        Entry e;
        e.name = name;
        e.dir = isDir;
        if (!isDir) {
            File f = SD.open(full);
            e.size = f ? f.size() : 0;
        }
        out.entries.push_back(e);
    }
    std::sort(out.entries.begin(), out.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return true;
}

installer::Check inspect(const String& path, Firmware& fw) {
    installer::Check c;
    String name = baseName(path);
    fw = Firmware();
    fw.name = endsWithBin(name) ? name.substring(0, name.length() - 4) : name;
    if (fw.name.length() > 40) fw.name = fw.name.substring(0, 40);
    fw.id = "sd:" + name;  // installing the same file again updates it
    fw.author = "SD card";
    fw.about = "From the SD card: " + path;
    fw.color = catalog::tileColor(fw.name.c_str());

    File f = SD.open(path);
    if (!f) {
        refuse(c, "The file can't be read");
        return c;
    }
    fw.size = f.size();

    // A full flash image starts with the bootloader and has a partition table at 0x8000;
    // an app-only image starts with the app itself.
    uint8_t magic = 0;
    readAt(f, 0, &magic, 1);
    if (magic != IMAGE_MAGIC) {
        refuse(c, "This isn't a firmware file");
        return c;
    }
    installer::Manifest& m = c.manifest;
    m.sdPath = path;
    constexpr size_t TABLE_MAX = 0xC00;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[TABLE_MAX]);
    uint8_t* table = buf.get();
    if (!table) {
        refuse(c, "Out of memory");
        return c;
    }
    bool merged = f.size() > parts::TABLE_OFFSET + 32 && readAt(f, parts::TABLE_OFFSET, table, 32) &&
                  table[0] == 0xAA && table[1] == 0x50;
    bool launcher = false;
    if (merged) {
        size_t n = std::min<size_t>(TABLE_MAX, f.size() - parts::TABLE_OFFSET);
        readAt(f, parts::TABLE_OFFSET, table, n);
        int appRank = 9;  // prefer factory, then ota_0, then any app
        for (size_t at = 0; at + 32 <= n && table[at] == 0xAA && table[at + 1] == 0x50; at += 32) {
            const uint8_t* p = table + at;
            uint8_t type = p[2], subtype = p[3];
            uint32_t offset, size;
            memcpy(&offset, p + 4, 4);
            memcpy(&size, p + 8, 4);
            char label[17] = {0};
            memcpy(label, p + 12, 16);
            if (type == parts::TYPE_APP) {
                int rank = subtype == parts::SUB_FACTORY ? 0 : subtype == parts::SUB_OTA_0 ? 1 : 2;
                uint8_t mg = 0;
                if (rank < appRank && offset < f.size() && readAt(f, offset, &mg, 1) && mg == IMAGE_MAGIC) {
                    appRank = rank;
                    m.appOffset = offset;
                    launcher = subtype == parts::SUB_TEST;
                }
            } else if (type == parts::TYPE_DATA &&
                       (subtype == parts::SUB_FAT || subtype == parts::SUB_SPIFFS || subtype == parts::SUB_LITTLEFS)) {
                installer::DataPart d;
                d.subtype = subtype;
                d.label = label;
                d.size = size;
                d.srcOffset = offset;
                d.copySize = f.size() > offset ? std::min<uint32_t>(size, f.size() - offset) : 0;
                m.data.push_back(d);
            }
        }
        if (appRank == 9) {
            refuse(c, "No app found inside this image");
            return c;
        }
    }

    esp_image_header_t h;
    if (!readAt(f, m.appOffset, &h, sizeof(h)) || h.magic != IMAGE_MAGIC) {
        refuse(c, "This isn't a firmware file");
        return c;
    }
    if (h.chip_id != CHIP_ESP32S3) {
        refuse(c, String("It was made for ") + chipName(h.chip_id) + ", so it won't run on the Cardputer");
        return c;
    }
    if (launcher) {
        refuse(c, "It's another launcher. It would take over the space SmartBoot manages for apps.");
        return c;
    }
    m.appSize = imageLength(f, m.appOffset, h);
    if (!m.appSize) {
        refuse(c, "The file is incomplete");
        return c;
    }
    esp_app_desc_t desc;
    if (readAt(f, m.appOffset + sizeof(h) + sizeof(esp_image_segment_header_t), &desc, sizeof(desc)) &&
        desc.magic_word == ESP_APP_DESC_MAGIC_WORD) {
        desc.version[sizeof(desc.version) - 1] = 0;
        desc.idf_ver[sizeof(desc.idf_ver) - 1] = 0;
        // Arduino builds put the framework version here ("esp-idf: v4.4.7 ..."), not the app's.
        if (desc.version[0] && !strstr(desc.version, "esp-idf") && strcmp(desc.version, desc.idf_ver))
            fw.version = desc.version;
    }
    m.version = fw.version;
    f.close();

    c.plan = installer::plan(fw, m);
    c.done = true;
    return c;
}

bool read(const String& path, uint32_t offset, uint32_t len,
          const std::function<bool(const uint8_t*, size_t)>& onChunk) {
    File f = SD.open(path);
    if (!f || !f.seek(offset)) return false;
    static uint8_t buf[4096];
    while (len) {
        size_t n = f.read(buf, std::min<size_t>(sizeof(buf), len));
        if (!n || !onChunk(buf, n)) return false;
        len -= n;
    }
    return true;
}

}  // namespace sdcard
