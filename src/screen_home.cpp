// Home: a carousel of big tiles. Installed apps come first, so the first app is
// selected when the Cardputer is turned on; then SmartBoot's own tiles.
#include <algorithm>
#include "apps.h"
#include "screens.h"

using ui::Icon;

namespace {

struct Item { const char* name; const char* desc; Icon icon; uint16_t color; };
const Item ITEMS[] = {
    {"Firmware Store", "Find and install new firmware", Icon::Store, rgb(0x4F8CFF)},
    {"SD Card", "Install .bin files from your SD card", Icon::SdCard, rgb(0x2FCB8A)},
    {"Wi-Fi", "Get online to download firmware", Icon::Wifi, rgb(0x9B7BFF)},
    {"Settings", "Brightness, sound and colors", Icon::Gear, rgb(0xFF9A3D)},
    {"About", "Device info and key guide", Icon::Info, rgb(0x6E8BB3)},
};
constexpr int FIXED = sizeof(ITEMS) / sizeof(ITEMS[0]);

class Home : public Screen {
    int sel = 0;
    float pos = 0;  // animated position of the carousel

    int count() const { return apps::count() + FIXED; }
    bool isApp(int i) const { return i < apps::count(); }
    const Item& fixed(int i) const { return ITEMS[i - apps::count()]; }
    uint16_t colorOf(int i) const { return isApp(i) ? rgb(apps::get(i).color) : fixed(i).color; }

    void launch(const App& app) {
        canvas.fillScreen(col::bg);
        canvas.setFont(font::title());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(col::text);
        canvas.drawString(String("Starting ") + app.name + "...", 120, 67);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
        String err;
        if (!apps::launch(app, &err)) ui::toast(err.c_str(), col::danger);  // restarts on success
    }

    void open(int i) {
        if (isApp(i)) return launch(apps::get(i));
        switch (i - apps::count()) {
            case 0: nav::push(screens::store()); break;
            case 1: nav::push(screens::sdBrowser()); break;
            case 2: nav::push(screens::wifi()); break;
            case 3: nav::push(screens::settings()); break;
            case 4: nav::push(screens::about()); break;
        }
    }

    // An installed app's tile: its color with the first letter of its name.
    void appTile(int i, int x, int cy, int size, float t) {
        uint16_t c = mix(colorOf(i), col::surface, t * 170);
        canvas.fillSmoothRoundRect(x - size / 2, cy - size / 2, size, size, size / 4, c);
        char letter[2] = {(char)toupper(apps::get(i).name[0]), 0};
        canvas.setFont(font::title());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(mix(TFT_WHITE, c, t * 120));
        canvas.drawString(letter, x, cy + 1);
    }

public:
    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Left: case Key::Up: sel = max(0, sel - 1); break;
            case Key::Right: case Key::Down: sel = min(count() - 1, sel + 1); break;
            case Key::Ok: open(sel); break;
            case Key::Tab:
                if (isApp(sel)) nav::push(screens::confirmRemove(apps::get(sel)));
                break;
            case Key::Char:
                if (e.ch >= '1' && e.ch <= '9' && e.ch - '1' < count()) open(e.ch - '1');
                break;
            default: break;
        }
    }
    bool update() override {
        sel = min(sel, count() - 1);  // an app may have been removed
        return ui::approach(pos, sel, 0.3f);
    }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar(APP_NAME);

        const int n = count(), cy = 49;
        // Far tiles first so the centre one overlaps its neighbours.
        int order[n];
        for (int i = 0; i < n; i++) order[i] = i;
        std::sort(order, order + n, [&](int a, int b) { return fabsf(a - pos) > fabsf(b - pos); });
        for (int i : order) {
            float d = i - pos;
            if (fabsf(d) > 2.5f) continue;
            float t = min(1.0f, fabsf(d));
            int size = 56 - 18 * t;
            int x = 120 + d * 70;
            if (isApp(i)) {
                appTile(i, x, cy, size, t);
            } else {
                uint16_t c = mix(fixed(i).color, col::surface, t * 170);
                canvas.fillSmoothRoundRect(x - size / 2, cy - size / 2, size, size, size / 4, c);
                ui::drawIcon(fixed(i).icon, x, cy, size * 0.58f, mix(TFT_WHITE, c, t * 120), c);
            }
        }
        canvas.drawRoundRect(120 - 32, cy - 32, 64, 64, 17, mix(colorOf(sel), col::bg, 110));

        String name = isApp(sel) ? apps::get(sel).name : String(fixed(sel).name);
        String desc = isApp(sel) ? "Installed app" + (apps::get(sel).version.length() ? " - " + apps::get(sel).version : String())
                                 : String(fixed(sel).desc);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        String shown = name;
        while (shown.length() > 3 && canvas.textWidth(shown) > 228) shown.remove(shown.length() - 1);
        if (shown != name) shown += "..";
        canvas.drawString(shown, 120, 84);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString(desc, 120, 101);

        // Page dots
        int x = 120 - (n * 6 + 6) / 2;
        for (int i = 0; i < n; i++) {
            if (i == sel) {
                canvas.fillSmoothRoundRect(x, 113, 10, 4, 2, accent());
                x += 14;
            } else {
                canvas.fillSmoothCircle(x + 2, 115, 2, isApp(i) ? col::muted : col::faint);
                x += 6;
            }
        }
        if (isApp(sel))
            ui::footer({{"lr", "choose"}, {"ok", "start"}, {"tab", "remove"}});
        else
            ui::footer({{"lr", "choose"}, {"ok", "open"}, {"1-9", "jump"}});
    }
};

Home homeScreen;

}  // namespace

namespace screens {
Screen* home() { return &homeScreen; }
}
