#include "apps.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_flash.h>
#include "config.h"
#include "partitions.h"

namespace {
constexpr const char* NS = "apps";  // in SB_NVS: label -> {"id","n","v","c","d":[...]}
std::vector<App> list;

// The name and version an image carries itself (esp_app_desc_t follows the image header
// and the first segment header). Used when the registry has no entry for an app.
bool readDescription(uint32_t offset, String& name, String& version) {
    esp_app_desc_t d;
    uint32_t at = offset + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (esp_flash_read(nullptr, &d, at, sizeof(d)) != ESP_OK || d.magic_word != ESP_APP_DESC_MAGIC_WORD)
        return false;
    d.project_name[sizeof(d.project_name) - 1] = 0;
    d.version[sizeof(d.version) - 1] = 0;
    name = d.project_name;
    version = d.version;
    return name.length() > 0;
}

void parseEntry(App& a, const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    a.id = doc["id"] | "";
    a.name = doc["n"] | "";
    a.version = doc["v"] | "";
    a.color = doc["c"] | 0u;
    for (const char* d : doc["d"].as<JsonArrayConst>()) a.data.push_back(d);
    for (const char* g : doc["g"].as<JsonArrayConst>()) a.grow.push_back(g);
}
}  // namespace

namespace apps {

void begin() {
    reload();
    parts::Table t;
    if (parts::read(t) && parts::bootSelectsApp(t)) parts::clearBoot(t);
}

void reload() {
    list.clear();
    parts::Table t;
    if (!parts::read(t)) return;
    Preferences p;
    bool open = p.begin(NS, true, SB_NVS);
    for (const parts::Entry& e : t.entries) {
        if (!e.isOtaApp()) continue;
        App a;
        a.label = e.label;
        a.size = e.size;
        if (open && p.isKey(e.label)) parseEntry(a, p.getString(e.label));
        if (!a.name.length() && !readDescription(e.offset, a.name, a.version)) a.name = e.label;
        if (!a.color) a.color = 0x6E8BB3;
        list.push_back(a);
    }
    if (open) p.end();
}

int count() { return list.size(); }
const App& get(int i) { return list[i]; }

const App* byId(const String& id) {
    for (const App& a : list)
        if (id.length() && a.id == id) return &a;
    return nullptr;
}

void save(const App& app) {
    JsonDocument doc;
    doc["id"] = app.id;
    doc["n"] = app.name;
    doc["v"] = app.version;
    doc["c"] = app.color;
    JsonArray d = doc["d"].to<JsonArray>();
    for (const String& label : app.data) d.add(label);
    JsonArray g = doc["g"].to<JsonArray>();
    for (const String& label : app.grow) g.add(label);
    String json;
    serializeJson(doc, json);
    Preferences p;
    p.begin(NS, false, SB_NVS);
    p.putString(app.label.c_str(), json);
    p.end();
}

void forget(const String& label) {
    Preferences p;
    if (!p.begin(NS, false, SB_NVS)) return;
    p.remove(label.c_str());
    p.end();
}

bool launch(const App& app, String* error) {
    parts::Table t;
    if (!parts::read(t, error)) return false;
    const parts::Entry* e = parts::find(t, app.label.c_str());
    if (!e) {
        if (error) *error = "App not found";
        return false;
    }
    if (!parts::setBoot(t, *e, error)) return false;
    delay(100);
    esp_restart();
    return true;  // not reached
}

bool remove(const App& app, String* error) {
    parts::Table t;
    if (!parts::read(t, error)) return false;
    parts::remove(t, app.label.c_str());
    for (const String& d : app.data) {  // its data partitions, unless another app uses them too
        bool shared = false;
        for (const App& other : list)
            if (other.label != app.label)
                for (const String& od : other.data) shared |= od == d;
        if (!shared) parts::remove(t, d.c_str());
    }
    // The one app left gets all free space for its growable storage. That is a copy of the
    // current game for Game Station, so moving it loses nothing; the new area is erased.
    std::vector<parts::Entry> grown;
    const App* left = nullptr;
    for (const App& other : list)
        if (other.label != app.label) left = left ? nullptr : &other;
    if (left && list.size() == 2) {
        for (const String& g : left->grow) {
            parts::Table bigger = t;
            const parts::Entry* e = parts::find(t, g.c_str());
            if (!e) continue;
            uint32_t was = e->size;
            uint8_t subtype = e->subtype;
            parts::remove(bigger, g.c_str());
            parts::Entry n;
            uint32_t size = parts::largestFree(bigger) & ~(parts::SECTOR - 1);
            if (size > was && parts::addData(bigger, subtype, g.c_str(), size, &n)) {
                t = bigger;
                grown.push_back(n);
            }
        }
    }
    if (!parts::write(t, error)) return false;
    for (const parts::Entry& n : grown) esp_flash_erase_region(nullptr, n.offset, n.size);
    forget(app.label);
    reload();
    return true;
}

}  // namespace apps
