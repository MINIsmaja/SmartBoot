// Wi-Fi: network list, password entry, and the catalog download progress screen.
#include <vector>
#include "catalog.h"
#include "net.h"
#include "screens.h"

using ui::Glyph;
using ui::Icon;

namespace {

void signalBars(int x, int y, int bars, uint16_t on, uint16_t off) {
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 2;
        canvas.fillRect(x + i * 4, y - h, 3, h, i < bars ? on : off);
    }
}

void lock(int x, int y, uint16_t c) {
    canvas.fillRoundRect(x, y, 7, 5, 1, c);  // body
    canvas.drawFastVLine(x + 1, y - 3, 3, c);  // shackle
    canvas.drawFastVLine(x + 5, y - 3, 3, c);
    canvas.drawFastHLine(x + 2, y - 4, 3, c);
}

// ---- Network list ---------------------------------------------------------
class WifiScreen : public Screen {
    static constexpr int LIST_TOP = 52, LIST_HEIGHT = FOOTER_Y - LIST_TOP, ROW_H = 20;
    std::vector<net::Network> nets;
    int sel = 0;
    float scroll = 0;
    bool scanning = false;
    net::State lastState = net::State::Off;
    uint32_t lastDraw = 0;

    float targetScroll() const {
        float s = scroll;
        int top = sel * ROW_H, bottom = top + ROW_H;
        if (top < s) s = top;
        if (bottom > s + LIST_HEIGHT) s = bottom - LIST_HEIGHT;
        return constrain(s, 0.0f, (float)max(0, (int)nets.size() * ROW_H - LIST_HEIGHT));
    }

    // sel stays 0 while the list is empty (scanning, or nothing found), so it must be checked.
    bool hasSel() const { return sel >= 0 && sel < (int)nets.size(); }
    bool selectedIsSaved() const { return hasSel() && nets[sel].known; }

    void rescan() {
        if (net::startScan()) { scanning = true; nets.clear(); sel = 0; scroll = 0; }
    }

    void markKnown() {
        String saved = net::savedNetwork();
        for (net::Network& n : nets) n.known = saved.length() && n.ssid == saved;
    }

    void connectTo(const net::Network& n) {
        bool alreadyOn = net::state() == net::State::Online && net::ssid() == n.ssid;
        if (alreadyOn) {
            ui::toast("Already connected", col::success);
        } else if (n.known) {
            net::connectSaved();
            ui::toast("Connecting...", col::warning);
        } else if (n.open) {
            net::connect(n.ssid, "", true);
            ui::toast("Connecting...", col::warning);
        } else {
            nav::push(screens::wifiPassword(n.ssid, false));
        }
    }

public:
    void onEnter() override {
        input::setTextMode(false);
        lastState = net::state();
        rescan();
    }

    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: sel = max(0, sel - 1); break;
            case Key::Down: sel = max(0, min((int)nets.size() - 1, sel + 1)); break;
            case Key::Ok:
                if (hasSel()) connectTo(nets[sel]);
                break;
            case Key::Char:
                if (e.ch == 'r' || e.ch == 'R') {
                    rescan();
                } else if ((e.ch == 'f' || e.ch == 'F') && selectedIsSaved()) {
                    net::forget();
                    markKnown();
                    ui::toast("Network forgotten", col::warning);
                }
                break;
            case Key::Back: nav::pop(); break;
            default: break;
        }
    }

    // Redraw only when something changed (or once a second for signal/status).
    bool update() override {
        bool dirty = false;
        if (scanning) {
            int n = net::scanResults(nets);
            if (n >= 0) {
                scanning = false;
                sel = min(sel, max(0, (int)nets.size() - 1));
                dirty = true;
            }
        }
        net::State st = net::state();
        if (st != lastState) {
            if (lastState == net::State::Connecting && st == net::State::Online)
                ui::toast("Connected!", col::success);
            else if (lastState == net::State::Connecting && st == net::State::Failed)
                ui::toast("Couldn't connect", col::danger);
            lastState = st;
            markKnown();  // a successful connect may have just saved this network
            dirty = true;
        }
        dirty |= ui::approach(scroll, targetScroll());
        uint32_t every = scanning || st == net::State::Connecting ? 300 : 1000;
        if (millis() - lastDraw > every) dirty = true;
        if (dirty) lastDraw = millis();
        return dirty;
    }

    void statusCard() {
        int y = STATUS_H + 2, h = 30;
        canvas.fillSmoothRoundRect(4, y, 232, h, 8, col::surface);
        net::State st = net::state();
        uint16_t dot = st == net::State::Online ? col::success
                       : st == net::State::Connecting ? col::warning
                       : st == net::State::Failed ? col::danger : col::faint;
        canvas.fillSmoothCircle(16, y + h / 2, 4, dot);
        canvas.setFont(font::body());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::text);
        String line1, line2;
        switch (st) {
            case net::State::Online:
                line1 = net::ssid();
                line2 = String("Online - ") + net::ip();
                break;
            case net::State::Connecting:
                line1 = "Connecting...";
                line2 = net::ssid();
                break;
            case net::State::Failed:
                line1 = "Can't connect";
                line2 = net::ssid() + " - check the password";
                break;
            default:
                line1 = "Not connected";
                line2 = "Pick a network below";
                break;
        }
        while (line1.length() > 3 && canvas.textWidth(line1) > 180) line1.remove(line1.length() - 1);
        canvas.drawString(line1, 28, y + 4);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        while (line2.length() > 3 && canvas.textWidth(line2) > 200) line2.remove(line2.length() - 1);
        canvas.drawString(line2, 28, y + 18);
        if (st == net::State::Online) signalBars(214, y + h / 2 + 5, net::bars(), col::success, col::surface2);
    }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Wi-Fi");
        statusCard();

        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::muted);
        canvas.drawString("NETWORKS", 6, STATUS_H + 36);
        if (scanning) {
            static const char* DOTS[] = {"scanning", "scanning.", "scanning..", "scanning..."};
            canvas.setTextDatum(top_right);
            canvas.setTextColor(col::faint);
            canvas.drawString(DOTS[(millis() / 300) % 4], 234, STATUS_H + 36);
        }

        canvas.setClipRect(0, LIST_TOP, SCREEN_W, LIST_HEIGHT);
        if (!scanning && nets.empty()) {
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_center);
            canvas.setTextColor(col::faint);
            canvas.drawString("No networks found", 120, LIST_TOP + LIST_HEIGHT / 2 - 6);
            canvas.setFont(font::small());
            canvas.drawString("press R to scan again", 120, LIST_TOP + LIST_HEIGHT / 2 + 10);
        }
        int y0 = LIST_TOP - (int)scroll;
        for (size_t i = 0; i < nets.size(); i++) {
            int y = y0 + (int)i * ROW_H;
            if (y + ROW_H < LIST_TOP || y > FOOTER_Y) continue;
            const net::Network& n = nets[i];
            bool on = (int)i == sel;
            uint16_t rowBg = on ? col::surface2 : col::surface;
            canvas.fillSmoothRoundRect(4, y, 228, ROW_H - 2, 5, rowBg);
            if (on) canvas.drawRoundRect(4, y, 228, ROW_H - 2, 5, accent());
            int tx = 12;
            if (n.known) {
                ui::drawIcon(Icon::Check, 15, y + ROW_H / 2 - 1, 11, col::success, rowBg);
                tx = 26;
            }
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_left);
            canvas.setTextColor(col::text);
            String name = n.ssid;
            while (name.length() > 3 && canvas.textWidth(name) > 150) name.remove(name.length() - 1);
            canvas.drawString(name, tx, y + ROW_H / 2);
            if (!n.open) lock(192, y + ROW_H / 2 - 2, col::muted);
            int b = 0;
            if (n.rssi >= -55) b = 4; else if (n.rssi >= -65) b = 3;
            else if (n.rssi >= -75) b = 2; else if (n.rssi >= -85) b = 1;
            signalBars(214, y + ROW_H / 2 + 5, b, on ? accent() : col::muted, col::surface2);
        }
        canvas.clearClipRect();
        ui::scrollbar(235, LIST_TOP, LIST_HEIGHT - 2, scroll, LIST_HEIGHT, nets.size() * ROW_H);

        if (selectedIsSaved())
            ui::footer({{"ok", "connect"}, {"R", "scan"}, {"F", "forget"}, {"esc", "back"}});
        else
            ui::footer({{"ud", "move"}, {"ok", "connect"}, {"R", "scan"}, {"esc", "back"}});
    }
};

// ---- Password entry -------------------------------------------------------
class WifiPassword : public Screen {
    String ssid, pass;
    bool reveal = false;
    bool caretOn = false;

    void leave() {
        input::setTextMode(false);
        nav::pop();
    }

public:
    void set(const String& s, bool) { ssid = s; pass = ""; reveal = false; }
    void onEnter() override { input::setTextMode(true); }
    bool update() override {  // redraw only when the caret blinks
        bool on = (millis() / 500) % 2;
        if (on == caretOn) return false;
        caretOn = on;
        return true;
    }

    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Char:
                if (e.ch >= 32 && e.ch <= 126 && pass.length() < 63) pass += e.ch;
                break;
            case Key::Back:
                if (pass.length()) pass.remove(pass.length() - 1);
                else leave();
                break;
            case Key::Tab: reveal = !reveal; break;
            case Key::Ok:
                if (pass.length() >= 8) {
                    net::connect(ssid, pass, true);
                    input::setTextMode(false);
                    nav::pop();  // back to the Wi-Fi list, which shows "Connecting"
                    ui::toast("Connecting...", col::warning);
                } else {
                    ui::toast("Password too short", col::danger);
                }
                break;
            default: break;
        }
    }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Wi-Fi password");

        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::muted);
        canvas.drawString("CONNECT TO", 10, 24);
        canvas.setFont(font::title());
        canvas.setTextColor(col::text);
        String s = ssid;
        while (s.length() > 3 && canvas.textWidth(s) > 220) s.remove(s.length() - 1);
        canvas.drawString(s, 10, 36);

        // input box
        int bx = 10, by = 62, bw = 220, bh = 24;
        canvas.fillSmoothRoundRect(bx, by, bw, bh, 7, col::surface);
        canvas.drawRoundRect(bx, by, bw, bh, 7, accent());
        canvas.setFont(font::body());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::text);
        int tx = bx + 8, cy = by + bh / 2, end;
        if (reveal) {
            String shown = pass;  // keep the end of a long password visible
            while (shown.length() && canvas.textWidth(shown) > bw - 20) shown.remove(0, 1);
            canvas.drawString(shown, tx, cy);
            end = tx + canvas.textWidth(shown);
        } else {
            const int step = 8;
            int n = min((int)pass.length(), (bw - 20) / step);
            for (int i = 0; i < n; i++) canvas.fillSmoothCircle(tx + 3 + i * step, cy, 3, col::text);
            end = tx + n * step;
        }
        if (caretOn) canvas.drawFastVLine(end + 1, by + 6, bh - 12, accent());

        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::faint);
        canvas.drawString(pass.length() < 8 ? "At least 8 characters" : "Press enter to connect",
                          bx + 2, by + bh + 5);

        ui::footer({{"ok", "connect"}, {"del", pass.length() ? "erase" : "back"}, {"tab", reveal ? "hide" : "show"}});
    }
};

// ---- Catalog download (connects to LauncherHub) ---------------------------
class Updating : public Screen {
    bool backToStore = false;
    bool done = false;
    uint32_t doneAt = 0;
    catalog::RefreshResult result{false, false, ""};

    void frame(const char* label, int pct) {
        canvas.fillScreen(col::bg);
        ui::statusBar("Firmware list");
        // cloud + down arrow
        int cx = 120, cy = 44;
        canvas.fillSmoothCircle(cx - 12, cy, 9, col::surface2);
        canvas.fillSmoothCircle(cx + 6, cy - 3, 12, col::surface2);
        canvas.fillSmoothRoundRect(cx - 14, cy, 30, 10, 5, col::surface2);
        canvas.fillTriangle(cx - 4, cy + 14, cx + 8, cy + 14, cx + 2, cy + 22, accent());
        canvas.fillRect(cx - 1, cy + 4, 6, 12, accent());

        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString("Updating the store", 120, 74);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString(label, 120, 94);
        if (pct >= 0) ui::progressBar(40, 110, 160, 6, pct / 100.0f, accent());
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
    }

public:
    void setReturn(bool b) { backToStore = b; }

    void onEnter() override {
        done = false;
        input::setTextMode(false);
        frame("Connecting to LauncherHub...", -1);
        result = catalog::refresh([&](int pct) { frame("Downloading firmware list...", pct); });
        done = true;
        doneAt = millis();
    }

    // The result screen shows the message; the toast repeats it on the screen below.
    void finish() {
        nav::pop();
        ui::toast(result.message.c_str(), result.ok ? col::success : col::danger);
    }

    void onKey(const KeyEvent& e) override {
        if (done && (e.key == Key::Ok || e.key == Key::Back)) finish();
    }

    bool update() override {
        if (done && millis() - doneAt > (result.ok ? 1200u : 2500u)) {
            finish();
            return false;
        }
        return !done;
    }

    void draw() override {
        if (!done) { frame("Working...", -1); return; }
        canvas.fillScreen(col::bg);
        ui::statusBar("Firmware list");
        ui::drawIcon(result.ok ? Icon::Check : Icon::Info, 120, 46, 40,
                     result.ok ? col::success : col::danger, col::bg);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString(result.ok ? "Up to date" : "Couldn't update", 120, 74);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        ui::wrapText(result.message.c_str(), 16, 94, 208, 11, col::muted, true);
        ui::footer({{"ok", "continue"}});
    }
};

WifiScreen wifiScreen;
WifiPassword passwordScreen;
Updating updatingScreen;

}  // namespace

namespace screens {
Screen* wifi() { return &wifiScreen; }
Screen* wifiPassword(const String& ssid, bool open) {
    passwordScreen.set(ssid, open);
    return &passwordScreen;
}
Screen* updating(bool backToStore) {
    updatingScreen.setReturn(backToStore);
    return &updatingScreen;
}
}
