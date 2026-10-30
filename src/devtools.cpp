// Serial commands for development:
//   shot              dump the current screen (hex RGB565 rows) for tools/screenshot.py
//   key <name>        press a key: up down left right ok back tab, or a single character
//   wifi <ssid> <pw>  connect to Wi-Fi and save the credentials
//   net               print the Wi-Fi state, IP and free heap
//   fakesaved <ssid>  pretend that network is the saved one (until reboot): tests being away from home
//   scan              run one Wi-Fi scan and print how many networks it found
//   sdspeed           raw SD read speed with 1, 8, 32 and 64 sectors per command
//   webabort          stop the upload page in the middle of a big upload (checks it ends cleanly)
//   usbpreview        open the USB drive's running screen without starting USB (for screenshots)
//   webtest           start the Wi-Fi upload page and test its endpoints from the device itself
//   usbdrive <secs>   share the SD card over USB for that long, then restart
//   refresh           download the catalog now (prints the result)
//   details <q>       fetch LauncherHub details of the first All match for <q>
//   plan <q>          show where that firmware would be installed (writes nothing)
//   install <q>       install it (no confirmation); <q> may be id:<LauncherHub fid> [name]
//   parts / apps      print the partition table / installed apps
//   parts rewrite     write the partition table back unchanged (tests table writes)
//   launch <n>        start installed app number n;  remove <n>  uninstall it
//   info              reset reasons, uptime, lowest free heap and loop stack
//   home              go back to the Home screen and clear the store search
//   sd [path]         list a folder of the SD card (all files);  sdcheck <file>  plan its install
#include "devtools.h"
#include "catalog.h"
#include "input.h"
#include "net.h"
#include "apps.h"
#include "installer.h"
#include "partitions.h"
#include "sdcard.h"
#include "usbdrive.h"
#include "webshare.h"
#include <WiFiClient.h>
#include <SD.h>
#include <ff.h>
#include <diskio.h>
#include "screens.h"
#include "ui.h"
#include <Preferences.h>
#include "config.h"

namespace {
String line;

void screenshot() {
    static const char HEX_DIGITS[] = "0123456789abcdef";
    static char row[SCREEN_W * 4 + 1];
    auto* px = (const uint8_t*)canvas.getBuffer();
    Serial.printf("SHOT %d %d\n", SCREEN_W, SCREEN_H);
    for (int y = 0; y < SCREEN_H; y++) {
        const uint8_t* p = px + y * SCREEN_W * 2;
        for (int i = 0; i < SCREEN_W * 2; i++) {
            row[i * 2] = HEX_DIGITS[p[i] >> 4];
            row[i * 2 + 1] = HEX_DIGITS[p[i] & 15];
        }
        row[SCREEN_W * 4] = '\n';
        Serial.write(row, SCREEN_W * 4 + 1);
    }
    Serial.println("END");
}

void pressKey(const String& name) {
    struct Named { const char* name; Key key; };
    static const Named NAMES[] = {{"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
                                  {"ok", Key::Ok}, {"back", Key::Back}, {"tab", Key::Tab}};
    for (const Named& n : NAMES) {
        if (name == n.name) { input::inject(n.key); Serial.println("OK"); return; }
    }
    if (name.length() == 1) { input::inject(Key::Char, name[0]); Serial.println("OK"); return; }
    Serial.println("ERR unknown key");
}

// Index of the first All-list firmware whose name contains q (any case), or -1.
int findFirmware(String q) {
    q.toLowerCase();
    for (int i = 0; i < catalog::allCount(); i++) {
        String n = catalog::all(i).name;
        n.toLowerCase();
        if (n.indexOf(q) >= 0) return i;
    }
    return -1;
}

// ---- Wi-Fi upload page self-test (calls its own endpoints over loopback) ----------------------
// Sends one HTTP/1.0 request to the local server; returns the status code (-1 = no connection).
// The body of the reply goes to `sink`.
int httpCall(const String& method, const String& path, const String& extraHeaders, const String& bodyHead,
             size_t dataLen, const String& bodyTail, const std::function<void(const uint8_t*, size_t)>& sink,
             uint8_t (*dataAt)(size_t) = nullptr) {
    WiFiClient c;
    if (!c.connect(IPAddress(127, 0, 0, 1), 80)) return -1;
    size_t total = bodyHead.length() + dataLen + bodyTail.length();
    c.print(method + " " + path + " HTTP/1.0\r\nHost: x\r\n" + extraHeaders + "Content-Length: " + String(total) + "\r\n\r\n");
    c.print(bodyHead);
    uint8_t buf[1024];
    for (size_t sent = 0; sent < dataLen;) {
        size_t n = min(sizeof(buf), dataLen - sent);
        for (size_t i = 0; i < n; i++) buf[i] = dataAt(sent + i);
        c.write(buf, n);
        sent += n;
    }
    c.print(bodyTail);

    int code = -1;
    bool inBody = false;
    String head;
    uint32_t quiet = millis();
    while ((c.connected() || c.available()) && millis() - quiet < 15000) {
        int n = c.read(buf, sizeof(buf));
        if (n <= 0) { delay(2); continue; }
        quiet = millis();
        const uint8_t* p = buf;
        if (!inBody) {
            for (int i = 0; i < n && !inBody; i++) {
                head += (char)buf[i];
                if (head.endsWith("\r\n\r\n")) {
                    inBody = true;
                    code = head.substring(9, 12).toInt();
                    p = buf + i + 1;
                    n -= i + 1;
                }
            }
            if (!inBody) continue;
        }
        sink(p, n);
    }
    c.stop();
    return code;
}

uint8_t patternAt(size_t i) { return (uint8_t)(i * 7 + 3); }

int simpleCall(const String& method, const String& path, String* body = nullptr) {
    String text;
    int code = httpCall(method, path, "", "", 0, "", [&](const uint8_t* p, size_t n) {
        if (body && text.length() < 3000) text.concat((const char*)p, n);
    });
    if (body) *body = text;
    return code;
}

void webTest() {
    int fails = 0;
    auto check = [&](const char* what, bool ok, const String& detail = "") {
        Serial.printf("  %s  %s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
        if (!ok) fails++;
    };
    String err;
    if (!webshare::start(&err)) { Serial.printf("FAIL: %s\n", err.c_str()); return; }
    Serial.printf("hotspot %s / %s at %s, heap=%u\n", webshare::ssid(), webshare::password(), webshare::address(),
                  (unsigned)ESP.getFreeHeap());
    delay(500);
    String body;
    uint32_t tPage = millis();
    int code = simpleCall("GET", "/", &body);
    check("page", code == 200 && body.startsWith("<!doctype html>"), String(code) + " after " + String(millis() - tPage) + " ms");
    code = simpleCall("GET", "/generate_204", &body);
    check("captive redirect", code == 302, String(code));
    code = simpleCall("GET", "/ls?p=/", &body);
    check("list root", code == 200 && body.indexOf("\"e\":[") > 0, String(code) + " " + body.substring(0, 80));
    code = simpleCall("GET", "/ls?p=/../etc", &body);
    check("refuses ..", code == 404, String(code));

    simpleCall("POST", "/rm?p=/_sbtest");  // leftovers of an earlier run
    code = simpleCall("POST", "/mkdir?p=/_sbtest/sub%20dir");
    check("mkdir -p", code == 200, String(code));

    const size_t SIZE = 300 * 1024;
    const String boundary = "----sbtest";
    String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"..\\evil:one?.bin\"\r\n"
                  "Content-Type: application/octet-stream\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    uint32_t t0 = millis();
    code = httpCall("POST", "/up?p=/_sbtest/sub%20dir", "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n",
                    head, SIZE, tail, [](const uint8_t*, size_t) {}, patternAt);
    uint32_t ms = millis() - t0;
    check("upload 300 KB", code == 200, String(code) + " in " + String(ms) + " ms = " + String(SIZE / 1024 * 1000 / max<uint32_t>(ms, 1)) + " KB/s");
    webshare::Status st = webshare::status();
    check("counters", st.files == 1 && st.totalBytes == SIZE, String(st.files) + " files, " + String(st.totalBytes) + " bytes");

    code = simpleCall("GET", "/ls?p=/_sbtest/sub%20dir", &body);
    check("name sanitized", code == 200 && body.indexOf("\"n\":\"evil_one_.bin\"") > 0 && body.indexOf("\"s\":" + String(SIZE)) > 0,
          body.substring(body.indexOf("[")));

    uint32_t sum = 0, want = 0;
    size_t got = 0;
    code = httpCall("GET", "/dl?p=/_sbtest/sub%20dir/evil_one_.bin", "", "", 0, "", [&](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++) sum = sum * 31 + p[i];
        got += n;
    });
    for (size_t i = 0; i < SIZE; i++) want = want * 31 + patternAt(i);
    check("download matches", code == 200 && got == SIZE && sum == want, String(got) + " bytes");

    code = simpleCall("POST", "/mv?p=/_sbtest/sub%20dir/evil_one_.bin&to=/_sbtest/renamed.bin");
    check("rename", code == 200, String(code));
    code = simpleCall("POST", "/mv?p=/_sbtest/renamed.bin&to=/_sbtest/renamed.bin");
    check("rename onto itself refused", code == 409, String(code));
    code = simpleCall("POST", "/rm?p=/");
    check("won't delete root", code == 400, String(code));
    code = simpleCall("POST", "/rm?p=/_sbtest");
    check("delete folder tree", code == 200, String(code));
    code = simpleCall("GET", "/ls?p=/_sbtest", &body);
    check("gone", code == 404, String(code));

    Serial.printf("stopping, heap before=%u\n", (unsigned)ESP.getFreeHeap());
    webshare::stop();
    Serial.printf("%s: heap after stop=%u, net=%s\n", fails ? "FAILED" : "ALL PASSED", (unsigned)ESP.getFreeHeap(), net::stateText());
}

// Streams a big upload to the local server until it is cut off (for webAbort).
volatile uint32_t abortSent = 0;
volatile bool abortSenderDone = false;
void abortSender(void*) {
    WiFiClient c;
    if (c.connect(IPAddress(127, 0, 0, 1), 80)) {
        const size_t SIZE = 20 * 1024 * 1024;
        String head = "--zz\r\nContent-Disposition: form-data; name=\"file\"; filename=\"abort.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n";
        String tail = "\r\n--zz--\r\n";
        c.print(String("POST /up?p=/_sbabort HTTP/1.0\r\nHost: x\r\nContent-Type: multipart/form-data; boundary=zz\r\nContent-Length: ") +
                String(head.length() + SIZE + tail.length()) + "\r\n\r\n" + head);
        static uint8_t chunk[1024];
        for (size_t i = 0; i < sizeof(chunk); i++) chunk[i] = patternAt(i);
        while (c.connected() && abortSent < SIZE) {
            if (c.write(chunk, sizeof(chunk)) == 0) break;
            abortSent = abortSent + sizeof(chunk);
        }
        c.stop();
    }
    abortSenderDone = true;
    vTaskDelete(nullptr);
}

// Leaving the upload screen while a file is arriving must neither hang nor leave the card locked.
void webAbort() {
    String err;
    if (!webshare::start(&err)) { Serial.printf("FAIL: %s\n", err.c_str()); return; }
    delay(500);
    abortSent = 0;
    abortSenderDone = false;
    xTaskCreatePinnedToCore(abortSender, "abortsend", 4096, nullptr, 1, nullptr, 1);
    while (webshare::status().totalBytes < 400 * 1024 && millis() < 20000) delay(20);
    Serial.printf("  receiving: %u KB so far, sender wrote %u KB\n", (unsigned)(webshare::status().totalBytes / 1024), (unsigned)(abortSent / 1024));
    uint32_t t0 = millis();
    webshare::stop();
    Serial.printf("  stop() took %u ms, heap=%u\n", (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
    for (uint32_t w = millis(); !abortSenderDone && millis() - w < 8000;) delay(20);
    Serial.printf("  sender finished=%d after %u KB\n", (int)abortSenderDone, (unsigned)(abortSent / 1024));

    // The card must still work, and no partial file may be left behind.
    bool mounted = sdcard::mount();
    sdcard::Listing l;
    bool listed = mounted && sdcard::list("/_sbabort", true, l);
    Serial.printf("  card usable=%d, partial file left=%d\n", (int)mounted, (int)(listed && l.entries.size() > 0));
    if (mounted) {
        SD.remove("/_sbabort/abort.bin");
        SD.rmdir("/_sbabort");
        sdcard::unmount();
    }
    Serial.printf("DONE net=%s heap=%u\n", net::stateText(), (unsigned)ESP.getFreeHeap());
}

void run(const String& cmd) {
    if (cmd == "shot") {
        screenshot();
    } else if (cmd.startsWith("key ")) {
        pressKey(cmd.substring(4));
    } else if (cmd.startsWith("wifi ")) {
        String rest = cmd.substring(5);
        int sp = rest.indexOf(' ');
        String ssid = sp < 0 ? rest : rest.substring(0, sp);
        String pass = sp < 0 ? "" : rest.substring(sp + 1);
        net::connect(ssid, pass, true);
        Serial.printf("OK connecting to %s\n", ssid.c_str());
    } else if (cmd == "offline") {
        net::disconnect();
        Serial.println("OK offline");
    } else if (cmd == "online") {
        net::connectSaved();
        Serial.println("OK reconnecting");
    } else if (cmd == "net") {
        Serial.printf("state=%s ssid=%s ip=%s rssi=%d heap=%u\n", net::stateText(),
                      net::ssid().c_str(), net::ip().c_str(), net::rssi(), (unsigned)ESP.getFreeHeap());
    } else if (cmd.startsWith("fakesaved ")) {  // like being away from home: the saved network isn't around
        net::devPretendSaved(cmd.substring(10));
        Serial.println("OK saved network is fake until reboot");
    } else if (cmd == "scan") {  // one scan through the same calls the Wi-Fi screen makes
        std::vector<net::Network> nets;
        uint32_t t0 = millis();
        if (!net::startScan()) { Serial.println("scan: already running"); return; }
        int n;
        while ((n = net::scanResults(nets)) < 0 && millis() - t0 < 20000) {
            net::poll();
            delay(20);
        }
        Serial.printf("scan: %d networks, %u ms, state=%s\n", n, (unsigned)(millis() - t0), net::stateText());
    } else if (cmd == "sdspeed") {  // raw read speed of the card for different sectors-per-command
        if (!sdcard::mount()) { Serial.println("FAIL: no SD card"); return; }
        for (int per : {1, 8, 32, 64}) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[per * 512]);
            const int total = 4096 * 1024 / 512;  // 4 MB
            uint32_t t0 = millis();
            int sector = 200000;
            for (int done = 0; done < total; done += per, sector += per)
                if (ff_disk_read(0, buf.get(), sector, per) != RES_OK) { Serial.println("read error"); return; }
            uint32_t ms = millis() - t0;
            Serial.printf("  %2d sectors per read: %u KB/s\n", per, (unsigned)(4096ull * 1000 / ms));
        }
    } else if (cmd == "webabort") {
        webAbort();
    } else if (cmd == "usbpreview") {  // screenshot the USB drive screen without starting USB
        nav::push(screens::usbDrivePreview());
        Serial.println("OK usb screen");
    } else if (cmd == "webtest") {
        webTest();
    } else if (cmd.startsWith("usbdrive ")) {  // share the SD card for N seconds, then restart
        int secs = cmd.substring(9).toInt();
        String err;
        Serial.println("OK serial goes away now; the device restarts when the time is up");
        Serial.flush();
        if (!usbdrive::start(&err)) { Serial.printf("FAIL: %s\n", err.c_str()); return; }
        uint32_t t0 = millis();
        while (millis() - t0 < (uint32_t)secs * 1000) delay(100);
        usbdrive::stop();
    } else if (cmd == "refresh") {
        Serial.println("refreshing...");
        auto r = catalog::refresh([](int pct) { Serial.printf("  %d%%\r", pct); });
        Serial.printf("\n%s: %s (rec=%d all=%d)\n", r.ok ? "OK" : "FAIL", r.message.c_str(),
                      catalog::recommendedCount(), catalog::allCount());
    } else if (cmd == "cat") {
        Serial.printf("cat rec=%d all=%d src=%d updated=%s\n", catalog::recommendedCount(),
                      catalog::allCount(), (int)catalog::source(), catalog::updated().c_str());
    } else if (cmd.startsWith("details ")) {
        int i = findFirmware(cmd.substring(8));
        if (i < 0) { Serial.println("not found"); return; }
        Firmware fw = catalog::loadDetails(catalog::all(i));
        String err;
        if (!catalog::fetchDetails(fw, &err)) { Serial.printf("FAIL: %s\n", err.c_str()); return; }
        Serial.printf("%s [%s] %s %s size=%u app=%u@%u\n  %s\n", fw.name.c_str(), fw.id.c_str(),
                      fw.version.c_str(), fw.date.c_str(), (unsigned)fw.size, (unsigned)fw.appSize,
                      (unsigned)fw.appOffset, fw.url.c_str());
    } else if (cmd == "parts") {
        parts::Table t;
        String err;
        if (!parts::read(t, &err)) { Serial.printf("FAIL: %s\n", err.c_str()); return; }
        for (const parts::Entry& e : t.entries)
            Serial.printf("  %-16s %s 0x%02x  0x%06x  %5u KB\n", e.label, e.isApp() ? "app " : "data",
                          e.subtype, (unsigned)e.offset, (unsigned)(e.size / 1024));
        Serial.printf("largest free: %u KB, boot -> %s\n", (unsigned)(parts::largestFree(t) / 1024),
                      parts::bootSelectsApp(t) ? "app" : "SmartBoot");
    } else if (cmd == "home") {  // back to a clean Home screen (for UI tests)
        catalog::setQuery("");
        nav::reset(screens::home());
        Serial.println("OK home");
    } else if (cmd == "sd" || cmd.startsWith("sd ")) {
        String path = cmd.length() > 3 ? cmd.substring(3) : String("/");
        sdcard::Listing l;
        if (!sdcard::mount()) { Serial.println("FAIL: no SD card"); return; }
        if (!sdcard::list(path, true, l)) { Serial.println("FAIL: not a folder"); return; }
        for (const sdcard::Entry& e : l.entries)
            Serial.printf("  %s%s %u\n", e.name.c_str(), e.dir ? "/" : "", (unsigned)e.size);
        Serial.printf("%u entries, %u hidden%s\n", (unsigned)l.entries.size(), l.hidden, l.truncated ? ", truncated" : "");
    } else if (cmd.startsWith("sdcheck ")) {
        if (!sdcard::mount()) { Serial.println("FAIL: no SD card"); return; }
        Firmware fw;
        installer::Check c = sdcard::inspect(cmd.substring(8), fw);
        const installer::Manifest& m = c.manifest;
        Serial.printf("%s %s: app %u KB @0x%x, %u data, %s\n", fw.name.c_str(), fw.version.c_str(),
                      (unsigned)(m.appSize / 1024), (unsigned)m.appOffset, (unsigned)m.data.size(),
                      c.plan.ok ? "fits" : c.plan.reason.c_str());
        for (const App& a : c.plan.removes) Serial.printf("  removes %s\n", a.name.c_str());
    } else if (cmd == "info") {
        Preferences p;
        p.begin("diag", true, SB_NVS);
        Serial.printf("reset=%d (last unexpected=%d, %u so far) up=%lus heap=%u min=%u stackfree=%u\n",
                      (int)esp_reset_reason(), p.getUChar("reason", 0), (unsigned)p.getUInt("count", 0),
                      millis() / 1000, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        p.end();
    } else if (cmd == "parts rewrite") {  // write the current table back unchanged
        parts::Table t;
        String err;
        bool ok = parts::read(t, &err) && parts::write(t, &err);
        Serial.println(ok ? "OK rewritten" : (String("FAIL: ") + err).c_str());
    } else if (cmd == "apps") {
        for (int i = 0; i < apps::count(); i++) {
            const App& a = apps::get(i);
            Serial.printf("  %d %s [%s] %s %u KB data=%u\n", i, a.name.c_str(), a.label.c_str(),
                          a.version.c_str(), (unsigned)(a.size / 1024), (unsigned)a.data.size());
        }
        Serial.printf("%d apps\n", apps::count());
    } else if (cmd.startsWith("plan ") || cmd.startsWith("install ")) {
        bool go = cmd.startsWith("install ");
        String q = cmd.substring(go ? 8 : 5);
        Firmware fw;
        if (q.startsWith("id:")) {  // any LauncherHub firmware, by ID: id:<fid> [name]
            int sp = q.indexOf(' ');
            fw.id = sp < 0 ? q.substring(3) : q.substring(3, sp);
            fw.name = sp < 0 ? fw.id : q.substring(sp + 1);
        } else {
            int i = findFirmware(q);
            if (i < 0) { Serial.println("not found"); return; }
            fw = catalog::loadDetails(catalog::all(i));
        }
        installer::Manifest m;
        String err;
        if (!catalog::fetchDetails(fw, &err) || !installer::fetchManifest(fw, m, &err)) {
            Serial.printf("FAIL: %s\n", err.c_str());
            return;
        }
        Serial.printf("%s %s: app %u KB @%u, %u data\n", fw.name.c_str(), m.version.c_str(),
                      (unsigned)(m.appSize / 1024), (unsigned)m.appOffset, (unsigned)m.data.size());
        for (const installer::DataPart& d : m.data)
            Serial.printf("  data %s 0x%02x %u KB, content %u KB\n", d.label.c_str(), d.subtype,
                          (unsigned)(d.size / 1024), (unsigned)(d.copySize / 1024));
        installer::Plan p = installer::plan(fw, m);
        if (!p.ok) { Serial.printf("PLAN FAIL: %s\n", p.reason.c_str()); return; }
        Serial.printf("plan: %s at 0x%06x (%u KB)%s\n", p.app.label, (unsigned)p.app.offset,
                      (unsigned)(p.app.size / 1024), p.replaces ? ", replaces the old version" : "");
        for (const installer::Fill& f : p.fills)
            Serial.printf("  fill %s at 0x%06x (%u KB) erase=%d copy=%u KB\n", f.part.label,
                          (unsigned)f.part.offset, (unsigned)(f.part.size / 1024), f.erase,
                          (unsigned)(f.copySize / 1024));
        for (const App& a : p.removes) Serial.printf("  removes %s\n", a.name.c_str());
        for (const String& sh : p.shrinks) Serial.printf("  %s\n", sh.c_str());
        if (!go) return;
        uint32_t t0 = millis();
        int lastPct = -1;
        auto r = installer::install(fw, m, p, [&](const char* stage, int pct) {
            if (pct / 10 != lastPct / 10) Serial.printf("  %s %d%%\n", stage, pct);
            lastPct = pct;
        });
        Serial.printf("%s: %s (%lus)\n", r.ok ? "OK" : "FAIL", r.message.c_str(), (millis() - t0) / 1000);
    } else if (cmd.startsWith("launch ") || cmd.startsWith("remove ")) {
        int i = cmd.substring(7).toInt();
        if (i < 0 || i >= apps::count()) { Serial.println("no such app"); return; }
        App a = apps::get(i);
        String err;
        if (cmd.startsWith("launch ")) {
            Serial.printf("launching %s\n", a.name.c_str());
            Serial.flush();
            if (!apps::launch(a, &err)) Serial.printf("FAIL: %s\n", err.c_str());
        } else {
            Serial.println(apps::remove(a, &err) ? "OK removed" : (String("FAIL: ") + err).c_str());
        }
    } else if (cmd.length()) {
        Serial.println("ERR: shot key wifi net refresh cat details|plan|install <q> parts apps launch|remove <n>");
    }
}
}  // namespace

namespace {
bool loading = false;
size_t loadIndex = 0;

// While sideloading a catalog, lines are catalog NDJSON until a lone "END".
void loadLine(const String& l) {
    if (l == "END") {
        catalog::importEnd();
        loading = false;
        Serial.printf("OK loaded (rec=%d all=%d)\n", catalog::recommendedCount(), catalog::allCount());
    } else {
        catalog::importLine(l, loadIndex++);
    }
}
}  // namespace

namespace devtools {
void begin() {
    esp_reset_reason_t r = esp_reset_reason();
    if (r != ESP_RST_PANIC && r != ESP_RST_INT_WDT && r != ESP_RST_TASK_WDT && r != ESP_RST_WDT &&
        r != ESP_RST_BROWNOUT)
        return;
    Preferences p;
    p.begin("diag", false, SB_NVS);
    p.putUChar("reason", r);
    p.putUInt("count", p.getUInt("count", 0) + 1);
    p.end();
}

void poll() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (loading) {
                loadLine(line);
            } else if (line == "load") {
                catalog::importBegin();
                loading = true;
                loadIndex = 0;
                Serial.println("OK send NDJSON lines, then END");
            } else {
                run(line);
            }
            line = "";
        } else if (line.length() < 6000) {
            line += c;
        }
    }
}
}
