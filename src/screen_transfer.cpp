// Transfer files: get MP3s, ROMs and other app files onto the SD card, by Wi-Fi from a phone or
// computer, or by plugging in a USB cable and using the card as a drive.
#include "net.h"
#include "screens.h"
#include "usbdrive.h"
#include "webshare.h"

using ui::Icon;

namespace {

String sizeText(uint32_t bytes) {
    char b[16];
    if (bytes < 1024) snprintf(b, sizeof(b), "%u B", (unsigned)bytes);
    else if (bytes < 1024 * 1024) snprintf(b, sizeof(b), "%u KB", (unsigned)(bytes / 1024));
    else snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0);
    return b;
}

String fit(String s, int maxW) {
    while (s.length() > 3 && canvas.textWidth(s) > maxW) s.remove(s.length() - 1);
    return s;
}

// A full-screen "working on it" frame, shown before a step that blocks for a moment.
void busyFrame(const char* title, const char* label) {
    canvas.fillScreen(col::bg);
    ui::statusBar(title);
    canvas.setFont(font::title());
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::text);
    canvas.drawString(label, 120, 66);
    canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// A message with a way out, for "no SD card" and similar.
void messageScreen(const char* title, const char* heading, const String& text) {
    canvas.fillScreen(col::bg);
    ui::statusBar(title);
    ui::drawIcon(Icon::Info, 120, 40, 36, col::danger, col::bg);
    canvas.setFont(font::title());
    canvas.setTextDatum(top_center);
    canvas.setTextColor(col::text);
    canvas.drawString(heading, 120, 64);
    canvas.setFont(font::small());
    ui::wrapText(text.c_str(), 16, 84, 208, 11, col::muted, true);
    ui::footer({{"esc", "back"}});
}

// ---- Wi-Fi upload page ------------------------------------------------------
class WifiShare : public Screen {
    String error;
    webshare::Status st;
    uint32_t lastDraw = 0;
    uint32_t lastBytes = 0;
    bool running = false;

    void leave() {
        if (running) {
            busyFrame("Wi-Fi upload", "Closing...");
            webshare::stop();
            running = false;
        }
        nav::pop();
    }

    void drawQr() {
        String code = String("WIFI:T:WPA;S:") + webshare::ssid() + ";P:" + webshare::password() + ";;";
        canvas.qrcode(code.c_str(), 6, 19, 100, 1, true);
    }

    void joinInfo() {
        const int x = 114, w = 122;
        canvas.setTextDatum(top_left);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString("1. Join this Wi-Fi", x, 21);
        canvas.setFont(font::body());
        canvas.setTextColor(col::text);
        canvas.drawString(fit(webshare::ssid(), w), x, 32);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString("Password", x, 52);
        canvas.setFont(font::title());
        canvas.setTextColor(accent());
        canvas.drawString(webshare::password(), x, 63);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString("2. Open the page", x, 86);
        canvas.setFont(font::body());
        canvas.setTextColor(col::text);
        canvas.drawString(webshare::address(), x, 97);
    }

    void activity() {
        const int x = 114, w = 122;
        canvas.fillSmoothCircle(x + 4, 28, 3, col::success);
        canvas.setFont(font::body());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::text);
        canvas.drawString(st.devices == 1 ? "1 device joined" : String(st.devices) + " devices joined", x + 12, 28);

        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::muted);
        canvas.drawString("Page didn't open? Go to", x, 44);
        canvas.setFont(font::body());
        canvas.setTextColor(col::text);
        canvas.drawString(webshare::address(), x, 55);

        canvas.fillSmoothRoundRect(x - 2, 76, w + 4, 42, 7, col::surface);
        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        if (st.receiving) {
            canvas.setTextColor(col::text);
            canvas.drawString(fit(st.file, w - 6), x + 3, 81);
            canvas.setTextColor(accent());
            canvas.drawString(String("Receiving ") + sizeText(st.fileBytes), x + 3, 95);
        } else if (st.files) {
            canvas.setTextColor(col::success);
            canvas.drawString(String(st.files) + (st.files == 1 ? " file" : " files") + " received", x + 3, 81);
            canvas.setTextColor(col::muted);
            canvas.drawString(sizeText(st.totalBytes) + " in total", x + 3, 95);
        } else {
            canvas.setTextColor(col::muted);
            canvas.drawString("Waiting for files...", x + 3, 88);
        }
    }

public:
    void onEnter() override {
        input::setTextMode(false);
        error = "";
        running = false;
        busyFrame("Wi-Fi upload", "Starting hotspot...");
        running = webshare::start(&error);
        st = webshare::status();
    }

    void onKey(const KeyEvent& e) override {
        if (e.key == Key::Back || (!running && e.key == Key::Ok)) leave();
    }

    bool update() override {
        if (!running) return false;
        webshare::Status now = webshare::status();
        if (now.receiving || now.totalBytes != lastBytes) input::touch();  // keep the screen on
        lastBytes = now.totalBytes;
        bool changed = now.devices != st.devices || now.receiving != st.receiving || now.files != st.files;
        bool progress = now.receiving && millis() - lastDraw > 250;
        st = now;
        if (changed || progress) lastDraw = millis();
        return changed || progress;
    }

    void draw() override {
        if (!running) return messageScreen("Wi-Fi upload", "Can't start", error);
        canvas.fillScreen(col::bg);
        ui::statusBar("Wi-Fi upload");
        drawQr();
        if (st.devices > 0) activity();
        else joinInfo();
        ui::footer({{"esc", "stop"}});
    }
};

// ---- USB drive --------------------------------------------------------------
class UsbDrive : public Screen {
    bool on = false;
    bool warned = false;  // pressed esc once after writing without ejecting
    uint32_t lastDraw = 0, lastBytes = 0;

    void line(int y, const char* label, const String& value, uint16_t color = col::text) {
        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::muted);
        canvas.drawString(label, 14, y);
        canvas.setTextDatum(top_right);
        canvas.setTextColor(color);
        canvas.drawString(value, 226, y);
    }

public:
    void onEnter() override {
        input::setTextMode(false);
        on = previewing;
        warned = false;
    }
    bool previewing = false;  // dev console screenshots: show the running screen without starting USB

    void onKey(const KeyEvent& e) override {
        if (!on) {
            if (e.key == Key::Back) nav::pop();
            if (e.key != Key::Ok) return;
            String err;
            busyFrame("USB drive", "Starting...");
            if (usbdrive::start(&err)) on = true;
            else ui::toast(err.c_str(), col::danger);
            return;
        }
        if (e.key != Key::Back) return;
        if (usbdrive::bytesWritten() && !usbdrive::ejected() && !warned) {
            warned = true;
            ui::toast("Eject it on the computer first (esc again to leave)", col::warning);
            return;
        }
        busyFrame("USB drive", "Restarting...");
        usbdrive::stop();  // restarts the device
    }

    bool update() override {
        if (!on) return false;
        uint32_t bytes = usbdrive::bytesRead() + usbdrive::bytesWritten();
        if (bytes != lastBytes) input::touch();
        bool dirty = bytes != lastBytes ? millis() - lastDraw > 250 : millis() - lastDraw > 1000;
        lastBytes = bytes;
        if (dirty) lastDraw = millis();
        return dirty;
    }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("USB drive");
        if (!on) {
            ui::drawIcon(Icon::SdCard, 120, 38, 40, accent(), col::bg);
            canvas.setFont(font::title());
            canvas.setTextDatum(top_center);
            canvas.setTextColor(col::text);
            canvas.drawString("Use the SD card as a drive", 120, 62);
            canvas.setFont(font::small());
            ui::wrapText("Plug into a computer and the card shows up like a USB stick. SmartBoot waits meanwhile.",
                         14, 82, 212, 11, col::muted, true);
            ui::footer({{"ok", "start"}, {"esc", "back"}});
            return;
        }

        bool ready = usbdrive::hostConnected();
        bool out = usbdrive::ejected();
        uint16_t dot = out ? col::success : ready ? col::success : col::warning;
        canvas.fillSmoothCircle(18, 31, 5, dot);
        canvas.setFont(font::title());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::text);
        canvas.drawString(out ? "Ejected" : ready ? "Connected" : "Waiting for USB", 30, 31);

        canvas.fillSmoothRoundRect(8, 50, 224, 36, 8, col::surface);
        line(56, "Read by the computer", sizeText(usbdrive::bytesRead()));
        line(70, "Written to the card", sizeText(usbdrive::bytesWritten()));

        canvas.setFont(font::small());
        ui::wrapText(out ? "Safe to leave. Press esc to go back to SmartBoot."
                         : "Eject the drive on your computer before you press esc.",
                     14, 94, 212, 11, out ? col::success : col::warning, true);
        ui::footer({{"esc", "finish"}});
    }
};

// ---- Menu -------------------------------------------------------------------
class Transfer : public Screen {
    struct Item { const char* title; const char* desc; Icon icon; };
    static constexpr int COUNT = 2;
    const Item items[COUNT] = {
        {"Wi-Fi upload", "From a phone or computer, no cable needed.", Icon::Wifi},
        {"USB drive", "Plug in and use the SD card like a USB stick.", Icon::SdCard},
    };
    int sel = 0;
    float hl = 0;

public:
    void onEnter() override { input::setTextMode(false); }

    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: sel = max(0, sel - 1); break;
            case Key::Down: sel = min(COUNT - 1, sel + 1); break;
            case Key::Ok: nav::push(sel == 0 ? screens::wifiShare() : screens::usbDrive()); break;
            case Key::Back: nav::pop(); break;
            default: break;
        }
    }

    bool update() override { return ui::approach(hl, sel); }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Transfer files");
        const int cardH = 46, gap = 6, top = CONTENT_Y + 5;
        for (int i = 0; i < COUNT; i++) {
            int y = top + i * (cardH + gap);
            bool on = i == sel;
            canvas.fillSmoothRoundRect(6, y, 228, cardH, 8, on ? col::surface2 : col::surface);
            if (on) canvas.drawRoundRect(6, y, 228, cardH, 8, accent());
            ui::drawIcon(items[i].icon, 30, y + cardH / 2, 28, on ? accent() : col::muted,
                         on ? col::surface2 : col::surface);
            canvas.setFont(font::title());
            canvas.setTextDatum(top_left);
            canvas.setTextColor(col::text);
            canvas.drawString(items[i].title, 52, y + 6);
            canvas.setFont(font::small());
            ui::wrapText(items[i].desc, 52, y + 24, 176, 10, col::muted);
        }
        ui::footer({{"ud", "move"}, {"ok", "open"}, {"esc", "back"}});
    }
};

Transfer transferScreen;
WifiShare wifiShareScreen;
UsbDrive usbDriveScreen;

}  // namespace

namespace screens {
Screen* transfer() { return &transferScreen; }
Screen* wifiShare() { return &wifiShareScreen; }
Screen* usbDrive() { return &usbDriveScreen; }
Screen* usbDrivePreview() {
    usbDriveScreen.previewing = true;
    return &usbDriveScreen;
}
}
