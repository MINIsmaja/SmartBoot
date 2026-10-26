// Install flow follows M5Launcher's installFirmwareFromManifest / installFirmwareDynamic
// (https://github.com/bmorcelli/Launcher, MIT License, Copyright (c) 2023 shikarunochi;
// see licenses/M5Launcher-MIT.txt), with SmartBoot's own space planning.
#include "installer.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include <esp_flash.h>
#include <esp_image_format.h>
#include <esp_random.h>
#include "config.h"
#include "net.h"
#include "sdcard.h"

namespace {
using parts::Entry;
using parts::Table;

constexpr uint32_t DATA_DEFAULT = 0x70000;  // 448 KB: M5Launcher's data size on 8 MB flash
constexpr uint32_t BIG_DATA = 0x500000;     // M5Launcher's Cardputer threshold, see wantsRest()
constexpr uint32_t GROW_MIN = 0x100000;     // growable storage is not shrunk below 1 MB
constexpr size_t MAX_CANDIDATES = 10;       // apps considered for removal

// Other launchers manage the partition table themselves and would undo SmartBoot's.
const char* const LAUNCHERS[] = {
    "967e0377b9889c7b82f059fb8a30adda",  // M5Launcher
};

void setError(String* error, const String& message) {
    if (error) *error = message;
}

String urlEncode(const String& s) {
    String out;
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
            out += hex;
        }
    }
    return out;
}

String mb(uint32_t bytes) {
    char b[16];
    snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0f);
    return b;
}

// LauncherHub's download link wants a device ID (M5Launcher sends the Wi-Fi MAC). We send
// a random one made once on this device instead, so the real MAC stays private.
String deviceId() {
    Preferences p;
    p.begin("device", false, SB_NVS);
    String id = p.getString("hwid", "");
    if (id.length() != 17) {
        uint8_t b[6];
        esp_fill_random(b, sizeof(b));
        b[0] = (b[0] & 0xFC) | 0x02;  // locally administered, unicast
        char s[18];
        snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        id = s;
        p.putString("hwid", id);
    }
    p.end();
    return id;
}

// A 6-character partition label from the app name, made unique (M5Launcher's scheme).
String appLabel(const Table& t, const String& name) {
    String base;
    for (size_t i = 0; i < name.length() && base.length() < 6; i++) {
        char c = tolower(name[i]);
        if (isalnum((unsigned char)c)) base += c;
    }
    if (!base.length()) base = "app";
    while (base.length() < 6) base += '0';
    if (!parts::find(t, base.c_str())) return base;
    for (int i = 1; i < 100; i++) {
        String candidate = base.substring(0, i < 10 ? 5 : 4) + i;
        if (!parts::find(t, candidate.c_str())) return candidate;
    }
    return "app" + String(esp_random() % 1000);
}

// Firmware that asks for more than 5 MB of empty storage really uses it (Game Station
// copies each game there), so like M5Launcher it gets all the space that is left.
// Most others ask for Arduino's default 1.4 MB and use little of it.
bool wantsRest(const installer::DataPart& d) { return !d.copySize && d.size > BIG_DATA; }

// Data without content gets a modest size (the app formats it); data with content
// keeps its own layout, since a file system image can't shrink. For wantsRest() data
// this is the minimum.
uint32_t sizeFor(const installer::DataPart& d) {
    if (d.copySize) return std::max(d.size, d.copySize);
    return d.size ? std::min(d.size, DATA_DEFAULT) : DATA_DEFAULT;
}

bool isProtected(const char* label) {
    for (const char* keep : {"nvs", "otadata", "factory", "phy_init", SB_NVS, SB_FS})
        if (!strcmp(label, keep)) return true;
    return false;
}

bool usedBy(const std::vector<const App*>& apps, const String& label) {
    for (const App* a : apps)
        for (const String& d : a->data)
            if (d == label) return true;
    return false;
}

// One layout attempt: drop the apps in `mask` (and, unless keepOld, the older install of
// this firmware) first, then place the new app and its data. With keepOld the older
// install stays untouched while placing and only leaves the final table, so a failed
// update still has the old version.
installer::Plan tryLayout(const Table& now, const Firmware& fw, const installer::Manifest& m,
                          const App* old, const std::vector<App>& others, uint32_t mask, bool keepOld,
                          bool shrink) {
    installer::Plan p;
    Table t = now;
    std::vector<const App*> staying;
    for (size_t i = 0; i < others.size(); i++) {
        if (mask & (1u << i)) {
            p.removes.push_back(others[i]);
            parts::remove(t, others[i].label.c_str());
        } else {
            staying.push_back(&others[i]);
        }
    }
    if (old && !keepOld) {
        parts::remove(t, old->label.c_str());
        for (const installer::DataPart& d : m.data)  // regrown below; free it first so space stays in one piece
            if (wantsRest(d) && usedBy({old}, d.label)) parts::remove(t, d.label.c_str());
    }
    for (const App& gone : p.removes)  // their data, unless an app that stays uses it too
        for (const String& d : gone.data)
            if (!usedBy(staying, d) && !(old && usedBy({old}, d))) parts::remove(t, d.c_str());
    p.fallback = t;
    p.fallbackNeeded = !p.removes.empty() || (old && !keepOld);

    // With shrink, apps that stay give up their growable storage while the new app is
    // placed, and get back what is left (at least GROW_MIN) instead of being removed.
    // That storage only holds a disposable copy (Game Station's current game), so the
    // fallback table may keep it where it was.
    struct Regrow { String label, owner; uint8_t subtype; uint32_t was; };
    std::vector<Regrow> regrow;
    std::vector<String> growable;  // labels apps that stay may grow; never shared
    for (const App* a : staying)
        for (const String& g : a->grow) growable.push_back(g);
    if (shrink) {
        for (const App* a : staying)
            for (const String& g : a->grow)
                if (const Entry* e = parts::find(t, g.c_str())) {
                    regrow.push_back({g, a->name, e->subtype, e->size});
                    parts::remove(t, g.c_str());
                }
        if (regrow.empty()) {
            p.reason = "Not enough free space";
            return p;
        }
    }
    if (old) {
        p.replaces = true;
        p.old = *old;
    }

    String err;
    if (!parts::addApp(t, m.appSize, appLabel(t, fw.name).c_str(), &p.app, &err)) {
        p.reason = err;
        return p;
    }
    // Fixed-size data first, then the data that takes whatever space is left.
    std::vector<const installer::DataPart*> order;
    for (const installer::DataPart& d : m.data)
        if (!wantsRest(d)) order.push_back(&d);
    for (const installer::DataPart& d : m.data)
        if (wantsRest(d)) order.push_back(&d);
    for (const installer::DataPart* dp : order) {
        const installer::DataPart& d = *dp;
        if (isProtected(d.label.c_str())) {
            p.reason = "Its storage name clashes with SmartBoot's";
            return p;
        }
        uint32_t size = sizeFor(d);
        if (wantsRest(d)) size = std::max(size, std::min(d.size, parts::largestFree(t) & ~(parts::SECTOR - 1)));
        Entry* ex = parts::find(t, d.label.c_str());
        bool ownOld = old && usedBy({old}, d.label);  // this firmware's earlier install
        // Another app's growable storage is never shared: Game Station writes games straight
        // into it. (With shrink it is not in t right now, so check by name.)
        bool othersGrow = std::find(growable.begin(), growable.end(), d.label) != growable.end();
        if (othersGrow || (ex && (!ex->isData() || ex->subtype != d.subtype || (wantsRest(d) && !ownOld)))) {
            p.reason = String("Another app uses the storage name \"") + d.label + "\"";
            return p;
        }
        // Too small for the image it ships, or for the space a wantsRest() app can now get.
        if (ex && (d.copySize || wantsRest(d)) && ex->size < size) {
            parts::remove(t, d.label.c_str());
            ex = nullptr;
        }
        installer::Fill f;
        f.srcOffset = d.srcOffset;
        f.copySize = d.copySize;
        if (!ex) {
            if (!parts::addData(t, d.subtype, d.label.c_str(), size, &f.part, &err)) {
                p.reason = err;
                return p;
            }
            f.erase = true;
            p.fills.push_back(f);
        } else if (d.copySize) {
            f.part = *ex;
            f.erase = true;
            p.fills.push_back(f);
        }
        p.dataLabels.push_back(d.label);
    }
    for (size_t i = 0; i < regrow.size(); i++) {  // give the shrunk storage what is left
        uint32_t size = (parts::largestFree(t) / (regrow.size() - i)) & ~(parts::SECTOR - 1);
        size = std::min(size, regrow[i].was);
        Entry e;
        if (size < GROW_MIN || !parts::addData(t, regrow[i].subtype, regrow[i].label.c_str(), size, &e, &err)) {
            p.reason = "Not enough free space";
            return p;
        }
        if (e.size < regrow[i].was) p.shrinks.push_back(regrow[i].owner + "'s storage shrinks to " + mb(e.size));
    }
    if (old && keepOld) {
        parts::remove(t, old->label.c_str());
        for (const String& d : old->data)  // data the new version no longer uses
            if (!usedBy(staying, d) && std::find(p.dataLabels.begin(), p.dataLabels.end(), d) == p.dataLabels.end())
                parts::remove(t, d.c_str());
    }
    if (!parts::validate(t, &err)) {
        p.reason = err;
        return p;
    }
    p.app = *parts::find(t, p.app.label);  // OTA numbering may have shifted
    for (installer::Fill& f : p.fills) f.part = *parts::find(t, f.part.label);
    p.table = t;
    p.ok = true;
    return p;
}

// Erases ahead of the write position (64 KB blocks where aligned) and writes raw flash.
struct FlashWriter {
    uint32_t base, size, pos = 0, erased = 0;
    bool failed = false;

    bool write(const uint8_t* data, size_t n) {
        if (pos + n > size) return fail();
        while (erased < pos + n) {
            uint32_t at = base + erased;
            uint32_t len = (at % parts::APP_ALIGN == 0 && size - erased >= parts::APP_ALIGN) ? parts::APP_ALIGN
                                                                                            : parts::SECTOR;
            if (esp_flash_erase_region(nullptr, at, len) != ESP_OK) return fail();
            erased += len;
        }
        if (esp_flash_write(nullptr, data, base + pos, n) != ESP_OK) return fail();
        pos += n;
        return true;
    }
    bool fail() {
        failed = true;
        return false;
    }
};
}  // namespace

namespace installer {

bool fetchManifest(const Firmware& fw, Manifest& m, String* error) {
    m = Manifest();
    if (!fw.id.length() || !fw.version.length()) {
        setError(error, "Details aren't loaded");
        return false;
    }
    JsonDocument filter;
    JsonObject inst = filter["version"]["install"].to<JsonObject>();
    inst["app"]["source_offset"] = true;
    inst["app"]["image_size"] = true;
    JsonObject part = inst["partitions"].add<JsonObject>();
    for (const char* key : {"type", "subtype", "label", "size", "source", "source_offset", "copy_size"})
        part[key] = true;

    JsonDocument doc;
    net::FetchStatus st = net::getJson(
        String(HUB_API) + "/firmwares?fid=" + fw.id + "&version=" + urlEncode(fw.version), doc, filter);
    if (st.result != net::Fetch::Ok) {
        setError(error, st.error);
        return false;
    }
    JsonObjectConst install = doc["version"]["install"];
    if (install.isNull()) {
        setError(error, "No install information");
        return false;
    }
    m.version = fw.version;
    m.appOffset = install["app"]["source_offset"] | 0u;
    m.appSize = install["app"]["image_size"] | 0u;
    for (JsonObjectConst p : install["partitions"].as<JsonArrayConst>()) {
        String type = p["type"] | "", subtype = p["subtype"] | "";
        if (type == "app") {
            if (!p["source_offset"].isNull()) m.appOffset = p["source_offset"];
            if (p["copy_size"] | 0u) m.appSize = p["copy_size"];
            continue;
        }
        if (type != "data") continue;
        DataPart d;
        if (subtype == "spiffs") d.subtype = parts::SUB_SPIFFS;
        else if (subtype == "littlefs") d.subtype = parts::SUB_LITTLEFS;
        else if (subtype == "fat") d.subtype = parts::SUB_FAT;
        else continue;
        d.label = p["label"] | "";
        if (!d.label.length()) d.label = subtype == "fat" ? "ffat" : "spiffs";
        d.size = p["size"] | 0u;
        String source = p["source"] | "firmware";
        if (source != "firmware" && (p["copy_size"] | 0u)) {
            setError(error, "Its data comes in a separate file, which SmartBoot cannot install yet");
            return false;
        }
        d.srcOffset = p["source_offset"] | 0u;
        d.copySize = p["copy_size"] | 0u;
        m.data.push_back(d);
    }
    if (!m.appSize) {
        setError(error, "No app in this download");
        return false;
    }
    return true;
}

Plan plan(const Firmware& fw, const Manifest& m) {
    Plan none;
    for (const char* id : LAUNCHERS) {
        if (fw.id == id) {
            none.reason = "It's another launcher. It would take over the space SmartBoot manages for apps.";
            return none;
        }
    }
    Table now;
    String err;
    if (!parts::read(now, &err)) {
        none.reason = err;
        return none;
    }
    const App* old = apps::byId(fw.id);
    std::vector<App> others;
    for (int i = 0; i < apps::count(); i++)
        if (!old || apps::get(i).label != old->label) others.push_back(apps::get(i));
    size_t n = std::min(others.size(), MAX_CANDIDATES);
    others.resize(n);

    // Try removing nothing first, then as few apps as possible, freeing the least space.
    std::vector<uint32_t> masks;
    std::vector<uint32_t> freed(1u << n, 0);
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        masks.push_back(mask);
        for (size_t i = 0; i < n; i++)
            if (mask & (1u << i)) freed[mask] += others[i].size;
    }
    std::sort(masks.begin(), masks.end(), [&](uint32_t a, uint32_t b) {
        int ca = __builtin_popcount(a), cb = __builtin_popcount(b);
        return ca != cb ? ca < cb : freed[a] < freed[b];
    });
    Plan last;
    for (uint32_t mask : masks) {
        for (bool keepOld : {true, false}) {
            if (!old && !keepOld) continue;
            for (bool shrink : {false, true}) {  // shrink growable storage before removing an app
                Plan p = tryLayout(now, fw, m, old, others, mask, keepOld, shrink);
                if (p.ok) return p;
                if (!shrink) last = p;
            }
        }
    }

    // Nothing fits. Compare with the space apps could use if every app were removed.
    Table empty = now;
    for (const parts::Entry& e : now.entries)
        if (e.isOtaApp() || (e.isData() && !isProtected(e.label) && e.subtype != parts::SUB_OTADATA))
            parts::remove(empty, e.label);
    uint32_t need = (m.appSize + parts::APP_ALIGN - 1) / parts::APP_ALIGN * parts::APP_ALIGN;
    for (const DataPart& d : m.data) need += sizeFor(d);
    uint32_t room = parts::largestFree(empty);
    last.ok = false;
    last.removes.clear();
    if (need > room)
        last.reason = "Needs the whole device. It needs " + mb(need) + ", apps can use " + mb(room) + ".";
    return last;
}

Check check(const Firmware& fw) {
    Check c;
    if (!fetchManifest(fw, c.manifest, &c.error)) return c;
    c.plan = plan(fw, c.manifest);
    c.done = true;
    return c;
}

Result install(const Firmware& fw, const Manifest& m, const Plan& p,
               const std::function<void(const char*, int)>& onProgress) {
    String id = deviceId();
    uint64_t total = m.appSize, done = 0;
    for (const Fill& f : p.fills) total += std::min(f.copySize, f.part.size);
    auto report = [&](const char* stage) {
        if (onProgress) onProgress(stage, total ? (int)(done * 100 / total) : 0);
    };
    // Before the first write into space that apps being removed still own, take them out
    // of the table, so a power cut mid-install leaves no half-overwritten app listed. A
    // download that fails before writing anything keeps them.
    bool touched = false;
    auto touch = [&]() {
        if (touched) return true;
        touched = true;
        return !p.fallbackNeeded || parts::write(p.fallback);
    };
    auto fail = [&](const String& message) -> Result {
        apps::reload();
        return {false, message};
    };
    // Copies [offset, offset + length) of the download or SD file into w; "" when it worked.
    bool fromSd = m.sdPath.length() > 0;
    auto fetch = [&](uint32_t offset, uint32_t length, FlashWriter& w, const char* stage) -> String {
        auto chunk = [&](const uint8_t* d, size_t n) {
            if (!touch() || !w.write(d, n)) return false;
            done += n;
            report(stage);
            return true;
        };
        if (fromSd) return sdcard::read(m.sdPath, offset, length, chunk) || w.failed ? "" : "Couldn't read the SD card";
        net::FetchStatus st = net::downloadBinary(fw.url, offset, length, chunk, nullptr, id.c_str());
        if (st.result == net::Fetch::Ok || w.failed) return "";
        return st.error.length() ? st.error : String("Download failed");
    };

    const char* copying = fromSd ? "Copying from SD" : "Downloading";
    report(copying);
    FlashWriter app{p.app.offset, p.app.size};
    String err = fetch(m.appOffset, m.appSize, app, copying);
    if (app.failed) return fail("Couldn't write to flash");
    if (err.length()) return fail(err);

    report("Checking");
    esp_partition_pos_t pos = {p.app.offset, p.app.size};
    esp_image_metadata_t meta;
    if (esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta) != ESP_OK) return fail("The download was damaged");

    for (const Fill& f : p.fills) {
        report("Preparing storage");
        if (!touch()) return fail("Couldn't update the partition table");
        if (f.erase && esp_flash_erase_region(nullptr, f.part.offset, f.part.size) != ESP_OK)
            return fail("Couldn't prepare storage");
        if (!f.copySize) continue;
        FlashWriter data{f.part.offset, f.part.size};
        data.erased = f.part.size;
        err = fetch(f.srcOffset, std::min(f.copySize, f.part.size), data, fromSd ? "Copying data" : "Downloading data");
        if (data.failed) return fail("Couldn't write to flash");
        if (err.length()) return fail(err);
    }

    report("Finishing");
    if (!parts::write(p.table, &err)) return fail(err);
    for (const App& gone : p.removes) apps::forget(gone.label);
    if (p.replaces) apps::forget(p.old.label);
    App a;
    a.label = p.app.label;
    a.id = fw.id;
    a.name = fw.name;
    a.version = m.version;
    a.data = p.dataLabels;
    for (const DataPart& d : m.data)
        if (wantsRest(d)) a.grow.push_back(d.label);
    a.color = fw.color;
    apps::save(a);
    apps::reload();
    return {true, fw.name + " is installed"};
}

}  // namespace installer
