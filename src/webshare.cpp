#include "webshare.h"
#include <DNSServer.h>
#include <esp_mac.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include "net.h"
#include "sdcard.h"
#include "webshare_page.h"

namespace {
constexpr size_t MAX_LS = 300;       // entries per /ls reply; the page asks again for more
constexpr int MAX_DEPTH = 10;        // folders deep when deleting
constexpr uint32_t STOP_WAIT_MS = 4000;
const char ADDRESS[] = "192.168.4.1";

WebServer* server = nullptr;
DNSServer* dns = nullptr;
TaskHandle_t serverTask = nullptr;
volatile bool quit = false;
volatile bool taskRunning = false;

char apSsid[20], apPass[10];

// Progress shown on the Cardputer's screen. Written by the server task, read by the UI.
volatile bool receiving = false;
volatile uint32_t fileBytes = 0, totalBytes = 0;
volatile uint16_t filesIn = 0;
char fileName[48] = "";
volatile int64_t freeBytes = -1;  // at start; the page shows it minus what has arrived

File upFile;
String upPath;
bool upOk = false;

// ---- Paths -----------------------------------------------------------------
// Absolute, no "..", nothing a FAT card can't hold. Everything from the web is checked with this.
bool okPath(const String& p) {
    if (p.length() < 1 || p.length() > 250 || p[0] != '/') return false;
    if (p.indexOf("..") >= 0 || p.indexOf('\\') >= 0) return false;
    for (size_t i = 0; i < p.length(); i++)
        if ((uint8_t)p[i] < 0x20) return false;
    return true;
}

String cleanName(String n) {
    int slash = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
    if (slash >= 0) n = n.substring(slash + 1);
    for (size_t i = 0; i < n.length(); i++) {
        char c = n[i];
        if ((uint8_t)c < 0x20 || strchr("<>:\"|?*", c)) n.setCharAt(i, '_');
    }
    n.trim();
    if (n.length() > 100) n = n.substring(0, 100);
    if (n == "." || n == "..") n = "";
    return n;
}

String joinPath(const String& dir, const String& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

bool isDirectory(const String& path) {
    File f = SD.open(path);
    bool dir = f && f.isDirectory();
    f.close();
    return dir;
}

// mkdir -p
bool ensureDir(const String& path) {
    if (path == "/") return true;
    String cur;
    int start = 1;
    while (start <= (int)path.length()) {
        int slash = path.indexOf('/', start);
        if (slash < 0) slash = path.length();
        cur = path.substring(0, slash);
        if (cur.length() > 1 && !SD.exists(cur) && !SD.mkdir(cur)) return false;
        start = slash + 1;
    }
    return isDirectory(path);
}

// Takes the first entry out each round, so a folder of thousands of files needs no list in RAM.
bool removeTree(const String& path, int depth) {
    if (depth > MAX_DEPTH) return false;
    if (!isDirectory(path)) return SD.remove(path);
    for (;;) {
        bool dir = false;
        File d = SD.open(path);
        String child = d.getNextFileName(&dir);
        d.close();
        if (!child.length()) break;
        if (dir ? !removeTree(child, depth + 1) : !SD.remove(child)) return false;
    }
    return SD.rmdir(path);
}

// ---- Replies ---------------------------------------------------------------
void fail(int code, const char* msg) { server->send(code, "text/plain", msg); }

void jsonString(String& out, const String& s) {
    out += '"';
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((uint8_t)c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            out += b;
        } else {
            out += c;
        }
    }
    out += '"';
}

void handleList() {
    String p = server->arg("p");
    if (!p.length()) p = "/";
    uint32_t skip = server->arg("o").toInt();
    if (!okPath(p) || !isDirectory(p)) return fail(404, "No such folder");

    int64_t free = freeBytes < 0 ? -1 : max<int64_t>(0, freeBytes - (int64_t)totalBytes);
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "application/json", "");
    server->sendContent("{\"free\":" + String((long long)free) + ",\"e\":[");

    File dir = SD.open(p);
    bool isDir = false, first = true, more = false;
    size_t sent = 0;
    String chunk;
    for (String full = dir.getNextFileName(&isDir); full.length(); full = dir.getNextFileName(&isDir)) {
        if (skip) { skip--; continue; }
        if (sent >= MAX_LS) { more = true; break; }
        String name = full.substring(full.lastIndexOf('/') + 1);
        uint32_t size = 0;
        if (!isDir) {
            File f = SD.open(full);
            size = f ? f.size() : 0;
            f.close();
        }
        chunk = first ? "" : ",";
        chunk += "{\"n\":";
        jsonString(chunk, name);
        chunk += isDir ? ",\"d\":1,\"s\":0}" : ",\"d\":0,\"s\":" + String(size) + "}";
        server->sendContent(chunk);
        first = false;
        sent++;
    }
    dir.close();
    server->sendContent(more ? "],\"more\":true}" : "],\"more\":false}");
    server->sendContent("");
}

void handleUploadData() {
    HTTPUpload& up = server->upload();
    if (up.status == UPLOAD_FILE_START) {
        upOk = false;
        upFile.close();
        String dir = server->arg("p"), name = cleanName(up.filename);
        if (!okPath(dir) || !name.length() || !ensureDir(dir)) return;
        upPath = joinPath(dir, name);
        upFile = SD.open(upPath, "w");
        upOk = upFile;
        strlcpy(fileName, name.c_str(), sizeof(fileName));
        fileBytes = 0;
        receiving = true;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (quit) {  // the Cardputer's screen left file sharing: drop the connection
            upOk = false;
            server->client().stop();
        }
        if (upOk) {
            upOk = upFile.write(up.buf, up.currentSize) == up.currentSize;  // false when the card is full
            fileBytes = fileBytes + up.currentSize;
            totalBytes = totalBytes + up.currentSize;
        }
    } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
        bool complete = up.status == UPLOAD_FILE_END;
        upFile.close();
        if (upPath.length() && !(complete && upOk)) SD.remove(upPath);  // no half files
        if (complete && upOk) filesIn = filesIn + 1;
        upOk = upOk && complete;
        receiving = false;
    }
}

void handleUploadDone() {
    if (upOk) server->send(200, "text/plain", "ok");
    else fail(500, "Could not save (is the card full?)");
    upPath = "";
}

void handleMakeDir() {
    String p = server->arg("p");
    if (!okPath(p) || p == "/") return fail(400, "Bad folder name");
    if (!ensureDir(p)) return fail(500, "Could not create the folder");
    server->send(200, "text/plain", "ok");
}

void handleRemove() {
    String p = server->arg("p");
    if (!okPath(p) || p == "/" || !SD.exists(p)) return fail(400, "Nothing to delete");
    if (!removeTree(p, 0)) return fail(500, "Could not delete");
    server->send(200, "text/plain", "ok");
}

void handleRename() {
    String from = server->arg("p"), to = server->arg("to");
    if (!okPath(from) || !okPath(to) || from == "/" || !SD.exists(from)) return fail(400, "Bad name");
    if (SD.exists(to)) return fail(409, "That name is taken");
    if (!SD.rename(from, to)) return fail(500, "Could not rename");
    server->send(200, "text/plain", "ok");
}

void handleDownload() {
    String p = server->arg("p");
    if (!okPath(p) || isDirectory(p)) return fail(404, "No such file");
    File f = SD.open(p);
    if (!f) return fail(404, "No such file");
    String name = p.substring(p.lastIndexOf('/') + 1);
    server->sendHeader("Content-Disposition", "attachment; filename=\"" + cleanName(name) + "\"");
    server->streamFile(f, "application/octet-stream");
    f.close();
}

// Phones probe a made-up address when they join; answering with a redirect opens this page.
void handleOther() {
    server->sendHeader("Location", String("http://") + ADDRESS + "/");
    server->sendHeader("Cache-Control", "no-store");
    server->send(302, "text/plain", "");
}

void handlePage() {
    server->sendHeader("Cache-Control", "no-store");
    server->send_P(200, "text/html", WEBSHARE_PAGE);
}

void serverLoop(void*) {
    // Free space can take a few seconds on a big card; the first page waits for it.
    freeBytes = (int64_t)SD.totalBytes() - (int64_t)SD.usedBytes();
    while (!quit) {
        dns->processNextRequest();
        server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    taskRunning = false;
    vTaskDelete(nullptr);
}

void makeCredentials() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // from the chip, valid even with only the hotspot on
    snprintf(apSsid, sizeof(apSsid), "SmartBoot-%02X%02X", mac[4], mac[5]);
    static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/o, 1/l/i to misread
    for (size_t i = 0; i < 8; i++) apPass[i] = ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
    apPass[8] = 0;
}
}  // namespace

namespace webshare {

bool start(String* err) {
    if (server) return true;
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (!sdcard::mount()) return fail("No SD card found");

    net::pause();
    WiFi.mode(WIFI_AP);
    makeCredentials();
    if (!WiFi.softAP(apSsid, apPass, 1, 0, 4)) {
        WiFi.mode(WIFI_STA);
        net::resume();
        return fail("Couldn't start the hotspot");
    }

    server = new WebServer(80);
    server->on("/", HTTP_GET, handlePage);
    server->on("/ls", HTTP_GET, handleList);
    server->on("/up", HTTP_POST, handleUploadDone, handleUploadData);
    server->on("/mkdir", HTTP_POST, handleMakeDir);
    server->on("/rm", HTTP_POST, handleRemove);
    server->on("/mv", HTTP_POST, handleRename);
    server->on("/dl", HTTP_GET, handleDownload);
    server->onNotFound(handleOther);
    server->begin();
    dns = new DNSServer();
    dns->start(53, "*", WiFi.softAPIP());

    receiving = false;
    fileBytes = 0;
    totalBytes = 0;
    filesIn = 0;
    fileName[0] = 0;
    freeBytes = -1;
    quit = false;
    taskRunning = true;
    xTaskCreatePinnedToCore(serverLoop, "webshare", 10 * 1024, nullptr, 2, &serverTask, 0);
    return true;
}

void stop() {
    if (!server) return;
    quit = true;
    for (uint32_t t0 = millis(); taskRunning && millis() - t0 < STOP_WAIT_MS;) delay(10);
    if (taskRunning) vTaskDelete(serverTask);  // stuck in a long request; give up on it
    taskRunning = false;
    serverTask = nullptr;
    upFile.close();
    receiving = false;
    server->stop();
    delete server;
    server = nullptr;
    dns->stop();
    delete dns;
    dns = nullptr;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    sdcard::unmount();
    net::resume();
}

const char* ssid() { return apSsid; }
const char* password() { return apPass; }
const char* address() { return ADDRESS; }

Status status() {
    Status s;
    s.running = server != nullptr;
    if (!s.running) return s;
    s.devices = WiFi.softAPgetStationNum();
    s.receiving = receiving;
    strlcpy(s.file, fileName, sizeof(s.file));
    s.fileBytes = fileBytes;
    s.totalBytes = totalBytes;
    s.files = filesIn;
    return s;
}

}  // namespace webshare
