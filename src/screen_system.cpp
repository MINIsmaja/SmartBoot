// Settings, About and the placeholder screen for features coming in later steps.
#include "screens.h"
#include "settings.h"

using ui::Icon;

namespace {

class SettingsScreen : public Screen {
    enum Row { BRIGHTNESS, SOUND, ACCENT, DIM, RESET, ROW_COUNT };
    static constexpr int ROW_H = CONTENT_H / ROW_COUNT;
    static constexpr const char* LABELS[ROW_COUNT] = {"Brightness", "Key sounds", "Accent color", "Dim screen after", "Reset settings"};
    int sel = 0;
    float hl = 0;  // animated highlight row

    void change(int d) {
        Settings& s = settings::get();
        switch (sel) {
            case BRIGHTNESS:
                s.brightness = constrain(s.brightness + d, 1, BRIGHTNESS_LEVELS);
                settings::applyBrightness();
                break;
            case SOUND: s.sound = !s.sound; break;
            case ACCENT: s.accent = (s.accent + d + ACCENT_COUNT) % ACCENT_COUNT; break;
            case DIM: s.dimAfter = (s.dimAfter + d + DIM_OPTION_COUNT) % DIM_OPTION_COUNT; break;
            default: return;
        }
        settings::save();
    }

    void drawValue(int row, int y) {
        const Settings& s = settings::get();
        int cy = y + ROW_H / 2;
        canvas.setFont(font::small());
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(col::muted);
        switch (row) {
            case BRIGHTNESS:
                for (int i = 0; i < BRIGHTNESS_LEVELS; i++)
                    canvas.fillSmoothRoundRect(184 + i * 9, cy - 4, 7, 9, 2, i < s.brightness ? accent() : col::surface2);
                break;
            case SOUND:
                canvas.drawString(s.sound ? "On" : "Off", 200, cy);
                ui::toggle(206, cy - 6, s.sound);
                break;
            case ACCENT:
                canvas.drawString(ACCENTS[s.accent].name, 214, cy);
                canvas.fillSmoothCircle(224, cy, 5, accent());
                break;
            case DIM: canvas.drawString(DIM_OPTIONS[s.dimAfter].label, 228, cy); break;
            case RESET: canvas.setTextColor(col::faint); canvas.drawString("press enter", 228, cy); break;
        }
    }

public:
    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: sel = max(0, sel - 1); break;
            case Key::Down: sel = min(ROW_COUNT - 1, sel + 1); break;
            case Key::Left: change(-1); break;
            case Key::Right: change(+1); break;
            case Key::Ok:
                if (sel == RESET) {
                    settings::reset();
                    ui::toast("Settings reset", col::success);
                } else {
                    change(+1);
                }
                break;
            case Key::Back: nav::pop(); break;
            default: break;
        }
    }
    bool update() override { return ui::approach(hl, sel); }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Settings");
        int hy = CONTENT_Y + hl * ROW_H;
        canvas.fillRect(0, hy, SCREEN_W, ROW_H, col::surface);
        canvas.fillRoundRect(0, hy + 4, 3, ROW_H - 8, 1, accent());
        for (int i = 0; i < ROW_COUNT; i++) {
            int y = CONTENT_Y + i * ROW_H;
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_left);
            canvas.setTextColor(i == RESET ? col::danger : (i == sel ? col::text : col::muted));
            canvas.drawString(LABELS[i], 12, y + ROW_H / 2);
            drawValue(i, y);
        }
        ui::footer({{"ud", "move"}, {"lr", "change"}, {"esc", "back"}});
    }
};
constexpr const char* SettingsScreen::LABELS[];

class About : public Screen {
    float scroll = 0;
    int target = 0, contentH = 0;

    static const char* boardName() {
        switch (M5.getBoard()) {
            case m5::board_t::board_M5CardputerADV: return "Cardputer ADV";
            case m5::board_t::board_M5Cardputer: return "Cardputer";
            default: return "Unknown";
        }
    }

    int row(int y, const char* label, const String& value) {
        canvas.setFont(font::small());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::muted);
        canvas.drawString(label, 12, y + 7);
        canvas.setFont(font::body());
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(col::text);
        canvas.drawString(value, 228, y + 7);
        return y + 15;
    }

public:
    void onKey(const KeyEvent& e) override {
        int maxScroll = max(0, contentH - CONTENT_H);
        switch (e.key) {
            case Key::Up: target = max(0, target - 15); break;
            case Key::Down: target = min(maxScroll, target + 15); break;
            case Key::Ok: nav::push(screens::welcome()); break;
            case Key::Back: nav::pop(); break;
            default: break;
        }
    }
    bool update() override { return ui::approach(scroll, target); }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("About");
        canvas.setClipRect(0, CONTENT_Y, SCREEN_W, CONTENT_H);
        int y = CONTENT_Y + 5 - (int)scroll;

        canvas.fillSmoothRoundRect(10, y, 26, 26, 7, accent());
        ui::drawIcon(Icon::Power, 23, y + 13, 18, TFT_WHITE, accent());
        canvas.setFont(font::title());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::text);
        canvas.drawString(APP_NAME, 44, y);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString("Version " APP_VERSION, 44, y + 17);
        y += 33;

        uint64_t mac = ESP.getEfuseMac();
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)mac, (uint8_t)(mac >> 8),
                 (uint8_t)(mac >> 16), (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
        int battery = M5.Power.getBatteryLevel();

        y = row(y, "Device", boardName());
        y = row(y, "Chip", String(ESP.getChipModel()) + " rev " + ESP.getChipRevision());
        y = row(y, "Flash", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
        y = row(y, "Free memory", String(ESP.getFreeHeap() / 1024) + " KB");
        y = row(y, "Battery", battery >= 0 ? String(battery) + "%" : String("--"));
        y = row(y, "MAC address", macStr);
        y += 4;
        canvas.setFont(font::small());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::faint);
        canvas.drawString("Press enter to see the key guide again", 120, y);
        y += 14;

        contentH = y + (int)scroll - CONTENT_Y;
        canvas.clearClipRect();
        ui::scrollbar(235, CONTENT_Y + 2, CONTENT_H - 4, scroll, CONTENT_H, contentH);
        ui::footer({{"ud", "scroll"}, {"ok", "key guide"}, {"esc", "back"}});
    }
};

class ComingSoon : public Screen {
    const char* title = "";
    const char* message = "";
    Icon icon = Icon::Info;
    uint16_t color = 0;

public:
    void set(const char* t, Icon i, uint16_t c, const char* m) { title = t; icon = i; color = c; message = m; }
    void onKey(const KeyEvent& e) override {
        if (e.key == Key::Back || e.key == Key::Ok) nav::pop();
    }
    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar(title);
        canvas.fillSmoothRoundRect(100, 22, 40, 40, 11, color);
        ui::drawIcon(icon, 120, 42, 24, TFT_WHITE, color);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString("Coming soon", 120, 68);
        canvas.setFont(font::small());
        ui::wrapText(message, 16, 88, 208, 11, col::muted, true);
        ui::footer({{"esc", "back"}});
    }
};

SettingsScreen settingsScreen;
About aboutScreen;
ComingSoon comingSoonScreen;

}  // namespace

namespace screens {
Screen* settings() { return &settingsScreen; }
Screen* about() { return &aboutScreen; }
Screen* comingSoon(const char* title, Icon icon, uint16_t color, const char* message) {
    comingSoonScreen.set(title, icon, color, message);
    return &comingSoonScreen;
}
}
