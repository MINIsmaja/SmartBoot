#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

// Wi-Fi connection + HTTPS downloads. Keeps saved credentials in NVS and
// verifies TLS against the ESP32 built-in CA bundle (no insecure shortcuts).
namespace net {

enum class State : uint8_t { Off, Connecting, Online, Failed };

struct Network {
    String ssid;
    int8_t rssi;
    bool open;     // no password needed
    bool known;    // matches the saved network
};

void begin();                 // loads saved creds, starts STA mode
void poll();                  // call each loop; advances connection state
State state();
const char* stateText();
String ssid();                // SSID we are on / connecting to
int rssi();                   // signal of the current connection, or 0
int bars();                   // 0..4 signal strength for the icon
String ip();

bool haveSaved();             // a network is remembered
String savedNetwork();        // its SSID, or empty
void connect(const String& ssid, const String& pass, bool save);
void connectSaved();          // reconnect to the remembered network
void disconnect();
void forget();                // drop saved credentials
void pause();                 // let something else (the file-sharing hotspot) use the radio
void resume();                // back to normal: reconnect to the saved network
void devPretendSaved(const String& ssid);  // dev console: act as if ssid were the saved network (not stored)

// Async scan. startScan() returns false if a scan is already running.
bool startScan();
bool scanning();
int scanResults(std::vector<Network>& out);  // -1 = still running, else count

// HTTPS download, body delivered one line at a time (newline-separated).
enum class Fetch : uint8_t { Ok, NotModified, NoWifi, ConnectFailed, HttpError, Empty };
struct FetchStatus { Fetch result; int httpCode; String error; };

// etag is read and updated in place for conditional requests (304 handling).
// onLine(line, index) is called per body line; onProgress(0..100) may be null.
FetchStatus download(const String& url, String& etag,
                     const std::function<void(const String&, size_t)>& onLine,
                     const std::function<void(int)>& onProgress = nullptr);

// HTTPS GET of a JSON reply, parsed straight off the connection (no big text buffer).
// `filter` keeps only the fields we need; `date` (may be null) gets the server's date
// as YYYY-MM-DD.
FetchStatus getJson(const String& url, JsonDocument& doc, const JsonDocument& filter,
                    String* date = nullptr);

// Stream a binary file (firmware image): skip `skip` bytes, deliver up to `want`
// bytes (0 = until close) via onChunk; return false from onChunk to abort.
// deviceId, if given, is sent as the HWID header (LauncherHub's download counter).
FetchStatus downloadBinary(const String& url, size_t skip, size_t want,
                           const std::function<bool(const uint8_t*, size_t)>& onChunk,
                           const std::function<void(int)>& onProgress = nullptr,
                           const char* deviceId = nullptr);

}  // namespace net
