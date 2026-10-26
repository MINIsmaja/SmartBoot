#include "net.h"
#include "config.h"
#include <algorithm>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {
constexpr const char* USER_AGENT = "SmartBoot/0.3 (Cardputer)";

net::State curState = net::State::Off;
String curSsid, curPass;      // network being used or attempted
String savedSsid, savedPass;  // last network that connected successfully
bool pendingSave = false;     // store curSsid/curPass once they connect
uint32_t connectStart = 0;
uint32_t lastRetry = 0;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t RETRY_MS = 10000;
bool scanRunning = false;

Preferences prefs;

void loadSaved() {
    prefs.begin("net", true, SB_NVS);
    savedSsid = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");
    prefs.end();
}

void persist(const String& ssid, const String& pass) {
    prefs.begin("net", false, SB_NVS);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    savedSsid = ssid;
    savedPass = pass;
}

void startAttempt(const String& ssid, const String& pass) {
    curSsid = ssid;
    curPass = pass;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
    curState = net::State::Connecting;
    connectStart = millis();
}

// Connected to the network we asked for (status can lag behind a network switch).
bool onTarget(wl_status_t s) { return s == WL_CONNECTED && WiFi.SSID() == curSsid; }

// Feeds ArduinoJson from a network client. NetworkClientSecure::read() returns -1 while
// no data is buffered yet, and readBytes() treats that as an error, which cuts replies
// short ("IncompleteInput"). This waits for data instead, up to timeoutMs of silence.
struct PatientReader {
    NetworkClient& client;
    uint32_t timeoutMs;

    bool wait() {
        uint32_t start = millis();
        while (!client.available()) {
            if (!client.connected() || millis() - start > timeoutMs) return false;
            delay(1);
        }
        return true;
    }
    int read() { return wait() ? client.read() : -1; }
    size_t readBytes(char* buf, size_t n) {
        size_t got = 0;
        while (got < n && wait()) {
            int r = client.read((uint8_t*)buf + got, n - got);
            if (r <= 0) break;
            got += r;
        }
        return got;
    }
};

// HTTP Date header "Mon, 26 Oct 2026 10:05:04 GMT" -> "2026-10-26"; empty if unparsable.
String isoDate(const String& httpDate) {
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int day = 0, year = 0;
    char mon[4] = {0};
    if (sscanf(httpDate.c_str(), "%*3s, %d %3s %d", &day, mon, &year) != 3) return "";
    const char* m = strstr(MONTHS, mon);
    if (!m) return "";
    char out[11];
    snprintf(out, sizeof(out), "%04d-%02d-%02d", year, (int)(m - MONTHS) / 3 + 1, day);
    return out;
}
}  // namespace

namespace net {

void begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname("smartboot");
    loadSaved();
    if (savedSsid.length()) connectSaved();
}

void connect(const String& ssid, const String& pass, bool save) {
    pendingSave = save;  // credentials are only stored once they work
    startAttempt(ssid, pass);
}

void connectSaved() {
    if (!savedSsid.length()) return;
    pendingSave = false;
    startAttempt(savedSsid, savedPass);
}

void disconnect() {
    WiFi.disconnect();
    pendingSave = false;
    curState = State::Off;
}

void forget() {
    persist("", "");
    curSsid = curPass = "";
    disconnect();
}

void poll() {
    wl_status_t s = WiFi.status();
    switch (curState) {
        case State::Connecting:
            if (onTarget(s)) {
                curState = State::Online;
                if (pendingSave) {
                    persist(curSsid, curPass);
                    pendingSave = false;
                }
            } else if (millis() - connectStart > CONNECT_TIMEOUT_MS) {
                curState = State::Failed;
                lastRetry = millis();
                if (pendingSave) {  // a new password that didn't work is not retried
                    pendingSave = false;
                    WiFi.disconnect();
                }
            }
            break;
        case State::Online:
            if (s != WL_CONNECTED) curState = State::Connecting, connectStart = millis();
            break;
        case State::Failed:
            // Only the known-good network keeps retrying in the background.
            if (onTarget(s)) {
                curState = State::Online;
            } else if (savedSsid.length() && curSsid == savedSsid && millis() - lastRetry > RETRY_MS) {
                lastRetry = millis();
                startAttempt(savedSsid, savedPass);
            }
            break;
        default:
            break;
    }
}

State state() { return curState; }

const char* stateText() {
    switch (curState) {
        case State::Connecting: return "Connecting";
        case State::Online: return "Online";
        case State::Failed: return "Can't connect";
        default: return "Offline";
    }
}

String ssid() { return curSsid; }
int rssi() { return curState == State::Online ? WiFi.RSSI() : 0; }
String ip() { return curState == State::Online ? WiFi.localIP().toString() : String("--"); }
bool haveSaved() { return savedSsid.length() > 0; }
String savedNetwork() { return savedSsid; }

int bars() {
    if (curState != State::Online) return 0;
    int r = WiFi.RSSI();
    if (r >= -55) return 4;
    if (r >= -65) return 3;
    if (r >= -75) return 2;
    if (r >= -85) return 1;
    return 0;
}

bool startScan() {
    if (scanRunning) return false;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);  // async, hide duplicates off
    scanRunning = true;
    return true;
}

bool scanning() { return scanRunning; }

int scanResults(std::vector<Network>& out) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    scanRunning = false;
    out.clear();
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        Network net;
        net.ssid = WiFi.SSID(i);
        net.rssi = WiFi.RSSI(i);
        net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        net.known = savedSsid.length() && net.ssid == savedSsid;
        if (!net.ssid.length()) continue;  // hidden network
        // Mesh/repeater setups report one SSID several times: keep the strongest.
        bool dup = false;
        for (Network& o : out) {
            if (o.ssid == net.ssid) {
                if (net.rssi > o.rssi) o.rssi = net.rssi;
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(net);
    }
    WiFi.scanDelete();
    // Saved network first, then strongest signal.
    std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) {
        if (a.known != b.known) return a.known;
        return a.rssi > b.rssi;
    });
    return out.size();
}

FetchStatus getJson(const String& url, JsonDocument& doc, const JsonDocument& filter, String* date) {
    if (curState != State::Online) return {Fetch::NoWifi, 0, "Not connected to Wi-Fi"};

    NetworkClientSecure client;
    client.useBuiltinCACertBundle();
    client.setTimeout(12);

    HTTPClient http;
    http.setConnectTimeout(12000);
    http.setTimeout(12000);
    http.useHTTP10(true);  // no chunked encoding, so the body parses as a plain stream
    if (!http.begin(client, url)) return {Fetch::ConnectFailed, 0, "Bad URL"};
    http.setUserAgent(USER_AGENT);
    const char* collect[] = {"Date"};
    http.collectHeaders(collect, 1);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        String err = code < 0 ? HTTPClient::errorToString(code) : String("Server error ") + code;
        http.end();
        return {code < 0 ? Fetch::ConnectFailed : Fetch::HttpError, code, err};
    }
    PatientReader reader{http.getStream(), 12000};
    DeserializationError e = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
    if (date) *date = isoDate(http.header("Date"));
    http.end();
    if (e) return {Fetch::Empty, code, String("Bad reply from server (") + e.c_str() + ")"};
    return {Fetch::Ok, code, ""};
}

// Stream a binary file. Skips `skip` bytes, then delivers up to `want` bytes
// (0 = until the server closes) through onChunk. Used for firmware images. With a
// known length it asks for just that byte range; servers that ignore Range get the
// leading bytes skipped instead.
FetchStatus downloadBinary(const String& url, size_t skip, size_t want,
                           const std::function<bool(const uint8_t*, size_t)>& onChunk,
                           const std::function<void(int)>& onProgress, const char* deviceId) {
    if (curState != State::Online) return {Fetch::NoWifi, 0, "Not connected to Wi-Fi"};

    bool https = url.startsWith("https:");
    NetworkClientSecure secure;
    WiFiClient plain;
    WiFiClient* client;
    if (https) {
        secure.useBuiltinCACertBundle();
        secure.setTimeout(15);
        client = &secure;
    } else {
        client = &plain;
    }

    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // CDN links redirect
    if (!http.begin(*client, url)) return {Fetch::ConnectFailed, 0, "Bad URL"};
    http.setUserAgent(USER_AGENT);
    if (deviceId) http.addHeader("HWID", deviceId);
    if (want) http.addHeader("Range", String("bytes=") + skip + "-" + (skip + want - 1));

    int code = http.GET();
    if (code != HTTP_CODE_OK && code != HTTP_CODE_PARTIAL_CONTENT) {
        String err = code < 0 ? HTTPClient::errorToString(code) : String("HTTP error ") + code;
        http.end();
        return {Fetch::HttpError, code, err};
    }

    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[2048];
    size_t skipped = code == HTTP_CODE_PARTIAL_CONTENT ? skip : 0, written = 0;
    int lastPct = -1;
    uint32_t lastData = millis();

    while (http.connected() && (want == 0 || written < want)) {
        size_t avail = stream->available();
        if (avail) {
            size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
            lastData = millis();
            const uint8_t* p = buf;
            if (skipped < skip) {  // discard the leading bytes (merged-image header)
                size_t drop = min(n, skip - skipped);
                skipped += drop;
                p += drop;
                n -= drop;
            }
            if (want && written + n > want) n = want - written;
            if (n && !onChunk(p, n)) { http.end(); return {Fetch::HttpError, code, "Write failed"}; }
            written += n;
            if (onProgress && want) {
                int pct = (int)(written * 100 / want);
                if (pct != lastPct) { onProgress(pct); lastPct = pct; }
            }
        } else {
            if (millis() - lastData > 12000) break;
            delay(2);
        }
    }
    http.end();
    if (want && written < want) return {Fetch::Empty, code, String("Incomplete (") + written + "/" + want + ")"};
    return {Fetch::Ok, code, ""};
}

FetchStatus download(const String& url, String& etag,
                     const std::function<void(const String&, size_t)>& onLine,
                     const std::function<void(int)>& onProgress) {
    if (curState != State::Online) return {Fetch::NoWifi, 0, "Not connected to Wi-Fi"};

    bool https = url.startsWith("https:");
    NetworkClientSecure secure;
    WiFiClient plain;
    WiFiClient* client;
    if (https) {
        secure.useBuiltinCACertBundle();  // verify the server's certificate chain
        secure.setTimeout(12);
        client = &secure;
    } else {
        client = &plain;
    }

    HTTPClient http;
    http.setConnectTimeout(12000);
    http.setTimeout(12000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(*client, url)) return {Fetch::ConnectFailed, 0, "Bad URL"};
    http.setUserAgent(USER_AGENT);
    if (etag.length()) http.addHeader("If-None-Match", etag);
    const char* collect[] = {"ETag"};
    http.collectHeaders(collect, 1);

    int code = http.GET();
    if (code == HTTP_CODE_NOT_MODIFIED) { http.end(); return {Fetch::NotModified, code, ""}; }
    if (code != HTTP_CODE_OK) {
        String err = code == HTTP_CODE_NOT_FOUND ? String("Catalog file not found (404)")
                     : code < 0 ? HTTPClient::errorToString(code) : String("HTTP error ") + code;
        http.end();
        return {Fetch::HttpError, code, err};
    }

    int total = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    size_t readBytes = 0, index = 0;
    int lastPct = -1;
    String line;
    line.reserve(400);
    uint32_t lastData = millis();

    while (http.connected() && (total < 0 || readBytes < (size_t)total)) {
        while (stream->available()) {
            char c = stream->read();
            readBytes++;
            lastData = millis();
            if (c == '\n') {
                onLine(line, index++);
                line = "";
            } else if (c != '\r') {
                line += c;
            }
        }
        if (onProgress && total > 0) {
            int pct = (int)(readBytes * 100 / (size_t)total);
            if (pct != lastPct) { onProgress(pct); lastPct = pct; }
        }
        if (millis() - lastData > 9000) break;  // stalled
        delay(1);
    }
    if (line.length()) onLine(line, index++);

    String newEtag = http.header("ETag");
    if (newEtag.length()) etag = newEtag;
    http.end();

    if (index == 0) return {Fetch::Empty, code, "Empty response"};
    return {Fetch::Ok, code, ""};
}

}  // namespace net
