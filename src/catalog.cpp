#include "catalog.h"
#include "config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "net.h"
#include "recommended.h"

namespace {
// Cache of the "All" list: a header line {"schema":2,"updated":...}, then one
// {"id","n","a"} line per firmware, most downloaded first.
constexpr const char* CACHE_FILE = "/hub.ndjson";
constexpr const char* TMP_FILE   = "/hub.tmp";
constexpr const char* LIST_QUERY = "/firmwares?category=cardputer&order_by=downloads";
constexpr size_t MAX_CARDS = 150;  // keeps the All list's RAM index around 10 KB

std::vector<FwCard> cards;      // all (lean index)
std::vector<uint16_t> filterIdx;  // search results into cards
String theQuery, theUpdated;
catalog::Source theSource = catalog::Source::None;
bool fsReady = false;
bool theChecked = false;

File importTmp;
uint32_t importOff = 0;
size_t importIdx = 0;

// Varied but calm tile colors, chosen by hashing the name.
const uint32_t TILE_COLORS[] = {
    0x4F8CFF, 0x2FCB8A, 0x9B7BFF, 0xFF9A3D, 0xFF6FAE,
    0x3DD6C4, 0xE85D9E, 0xFFB547, 0x6E8BB3, 0x7E6BFF,
};
uint32_t colorFor(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return TILE_COLORS[h % (sizeof(TILE_COLORS) / sizeof(TILE_COLORS[0]))];
}

// Parse one cache line into the in-RAM index. offset = its byte position.
void addEntry(const String& line, size_t index, uint32_t offset) {
    if (line.length() < 2) return;
    if (cards.size() >= MAX_CARDS) return;
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;  // skip malformed lines
    if (index == 0) {
        theUpdated = doc["updated"] | "?";
        return;
    }
    const char* name = doc["n"] | "";
    if (!*name) return;
    FwCard c;
    c.off = offset;
    strlcpy(c.name, name, sizeof(c.name));
    strlcpy(c.author, doc["a"] | "", sizeof(c.author));
    c.color = colorFor(c.name);
    cards.push_back(c);
}

void clearAll() {
    cards.clear(); cards.shrink_to_fit();
    filterIdx.clear(); filterIdx.shrink_to_fit();
    theUpdated = "";
    theSource = catalog::Source::None;
}

void finishLoad(catalog::Source src) {
    // Loading reserves room for MAX_CARDS; give back what the list doesn't use.
    cards.shrink_to_fit();
    theSource = src;
    catalog::setQuery("");  // build the (empty) filter
}

bool loadCache() {
    if (!fsReady || !LittleFS.exists(CACHE_FILE)) return false;
    File f = LittleFS.open(CACHE_FILE, "r");
    if (!f) return false;
    clearAll();
    cards.reserve(MAX_CARDS);
    size_t index = 0;
    while (f.available()) {
        uint32_t pos = f.position();
        String line = f.readStringUntil('\n');
        addEntry(line, index++, pos);
    }
    f.close();
    if (cards.empty()) return false;
    finishLoad(catalog::Source::Cache);
    return true;
}

// No list downloaded yet: only the built-in Recommended tab.
void loadBuiltIn() {
    clearAll();
    finishLoad(catalog::Source::BuiltIn);
}

const RecInfo* recInfo(const String& id) {
    for (const RecInfo& r : RECOMMENDED)
        if (id == r.id) return &r;
    return nullptr;
}

Firmware fromRec(const RecInfo& r) {
    Firmware f;
    f.id = r.id;
    f.name = r.name;
    f.author = r.author;
    f.tagline = r.tagline;
    f.about = r.about;
    f.good = r.good;
    f.color = colorFor(r.name);
    f.recommended = true;
    return f;
}
}  // namespace

namespace catalog {

void begin() {
    fsReady = LittleFS.begin(true, "/littlefs", 10, SB_FS);  // format on first use
    if (fsReady) {  // files of the old GitHub catalog, replaced by LauncherHub
        for (const char* old : {"/catalog.ndjson", "/catalog.tmp", "/etag.txt"})
            if (LittleFS.exists(old)) LittleFS.remove(old);
    }
    if (!loadCache()) loadBuiltIn();
}

int recommendedCount() { return sizeof(RECOMMENDED) / sizeof(RECOMMENDED[0]); }
Firmware recommended(int i) { return fromRec(RECOMMENDED[i]); }
uint32_t tileColor(const char* name) { return colorFor(name); }
int allCount() { return cards.size(); }
const FwCard& all(int i) { return cards[i]; }

Firmware loadDetails(const FwCard& c) {
    Firmware f;
    f.name = c.name;
    f.author = c.author;
    f.color = c.color;
    if (!fsReady) return f;
    File file = LittleFS.open(CACHE_FILE, "r");
    if (!file) return f;
    file.seek(c.off);
    String line = file.readStringUntil('\n');
    file.close();
    JsonDocument doc;
    if (!deserializeJson(doc, line)) f.id = doc["id"] | "";
    if (const RecInfo* r = recInfo(f.id)) {  // same firmware as a recommended one
        f.name = r->name;
        f.color = colorFor(r->name);
        f.tagline = r->tagline;
        f.about = r->about;
        f.good = r->good;
        f.recommended = true;
    }
    return f;
}

bool fetchDetails(Firmware& fw, String* error) {
    if (!fw.id.length()) {
        if (error) *error = "Unknown firmware";
        return false;
    }
    JsonDocument filter;
    JsonObject v = filter["versions"].add<JsonObject>();
    for (const char* key : {"version", "file", "published_at", "Fs", "as", "ao"}) v[key] = true;

    JsonDocument doc;
    net::FetchStatus st = net::getJson(String(HUB_API) + "/firmwares?fid=" + fw.id, doc, filter);
    if (st.result != net::Fetch::Ok) {
        if (error) *error = st.error;
        return false;
    }
    JsonObjectConst latest = doc["versions"][0];  // newest first
    if (latest.isNull()) {
        if (error) *error = "No download listed";
        return false;
    }
    fw.version = latest["version"] | "";
    fw.date = latest["published_at"] | "";
    fw.size = latest["Fs"] | 0u;
    fw.appSize = latest["as"] | 0u;
    fw.appOffset = latest["ao"] | 0u;
    const char* file = latest["file"] | "";
    // Download through LauncherHub so it counts the install; it redirects to the CDN.
    fw.url = *file ? String(HUB_API) + "/download?fid=" + fw.id + "&file=" + FIRMWARE_CDN + file
                   : String();
    return true;
}

void setQuery(const String& q) {
    theQuery = q;
    filterIdx.clear();
    String needle = q;
    needle.toLowerCase();
    for (size_t i = 0; i < cards.size(); i++) {
        if (needle.length() == 0) { filterIdx.push_back(i); continue; }
        String hay = String(cards[i].name) + " " + cards[i].author;
        hay.toLowerCase();
        if (hay.indexOf(needle) >= 0) filterIdx.push_back(i);
    }
}
const String& query() { return theQuery; }
int filteredCount() { return filterIdx.size(); }
const FwCard& filtered(int i) { return cards[filterIdx[i]]; }

Source source() { return theSource; }
const String& updated() { return theUpdated; }
bool checkedOnline() { return theChecked; }

RefreshResult refresh(const std::function<void(int)>& onProgress) {
    // Free the current list first: the TLS handshake needs a big contiguous
    // allocation, and we are replacing the list anyway. Restore from cache if the
    // download fails.
    clearAll();
    cards.reserve(MAX_CARDS);  // fixed capacity: no realloc while TLS holds the heap

    File tmp = fsReady ? LittleFS.open(TMP_FILE, "w") : File();
    uint32_t off = 0;
    size_t index = 0;
    auto addLine = [&](const String& line) {
        addEntry(line, index++, off);
        if (tmp) { tmp.print(line); tmp.print('\n'); }
        off += line.length() + 1;
    };

    JsonDocument filter;
    filter["total"] = true;
    JsonObject item = filter["items"].add<JsonObject>();
    item["fid"] = true;
    item["name"] = true;
    item["author"] = true;

    net::FetchStatus st{net::Fetch::Ok, 0, ""};
    for (int page = 1; cards.size() < MAX_CARDS; page++) {  // 100 per page
        JsonDocument doc;
        String date;
        st = net::getJson(String(HUB_API) + LIST_QUERY + "&page=" + page, doc, filter,
                          page == 1 ? &date : nullptr);
        if (st.result != net::Fetch::Ok) break;
        if (page == 1) {
            JsonDocument header;
            header["schema"] = 2;
            header["updated"] = date.length() ? date : String("?");
            String line;
            serializeJson(header, line);
            addLine(line);
        }
        JsonArrayConst items = doc["items"];
        for (JsonObjectConst it : items) {
            if (cards.size() >= MAX_CARDS) break;
            JsonDocument entry;
            entry["id"] = it["fid"];
            entry["n"] = it["name"];
            entry["a"] = it["author"];
            String line;
            serializeJson(entry, line);
            addLine(line);
        }
        size_t want = min((size_t)(doc["total"] | 0), MAX_CARDS);
        if (onProgress && want) onProgress(min(100, (int)(cards.size() * 100 / want)));
        if (items.size() == 0 || cards.size() >= want) break;
    }
    if (tmp) tmp.close();
    // We reached the server (even on an error) -> don't auto-retry every store open.
    if (st.result != net::Fetch::NoWifi && st.result != net::Fetch::ConnectFailed) theChecked = true;

    if (st.result == net::Fetch::Ok && !cards.empty()) {
        finishLoad(Source::Network);
        if (fsReady) {
            LittleFS.remove(CACHE_FILE);
            LittleFS.rename(TMP_FILE, CACHE_FILE);
        }
        return {true, true, String("Updated: ") + cards.size() + " firmwares"};
    }

    if (fsReady) LittleFS.remove(TMP_FILE);
    if (!loadCache()) loadBuiltIn();  // restore what we freed above
    if (st.result == net::Fetch::NoWifi) return {false, false, "Connect to Wi-Fi first"};
    if (st.result == net::Fetch::Ok) return {false, false, "The firmware list was empty"};
    return {false, false, st.error.length() ? st.error : String("Download failed")};
}

void importBegin() {
    clearAll();
    cards.reserve(MAX_CARDS);
    importOff = 0;
    importIdx = 0;
    importTmp = fsReady ? LittleFS.open(TMP_FILE, "w") : File();
}
void importLine(const String& line, size_t index) {
    addEntry(line, index, importOff);
    if (importTmp) { importTmp.print(line); importTmp.print('\n'); }
    importOff += line.length() + 1;
    importIdx = index;
}
void importEnd() {
    if (importTmp) importTmp.close();
    if (!cards.empty()) {
        finishLoad(Source::Cache);
        if (fsReady) { LittleFS.remove(CACHE_FILE); LittleFS.rename(TMP_FILE, CACHE_FILE); }
    } else {
        if (fsReady) LittleFS.remove(TMP_FILE);
        if (!loadCache()) loadBuiltIn();
    }
}

}  // namespace catalog
