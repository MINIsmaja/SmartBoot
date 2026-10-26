// Splash animation and the first-run key guide.
#include "screens.h"
#include "settings.h"

using ui::Glyph;

namespace {

class Splash : public Screen {
    uint32_t start = 0;

    void finish() { nav::reset(settings::get().welcomed ? screens::home() : screens::welcome()); }

public:
    void onEnter() override { start = millis(); }
    void onKey(const KeyEvent&) override { finish(); }
    bool update() override {
        if (millis() - start > 1400) finish();
        return true;
    }
    void draw() override {
        float t = min(1.0f, (millis() - start) / 1200.0f);
        float e = 1 - powf(1 - min(1.0f, t * 1.8f), 3);  // ease-out for the logo
        canvas.fillScreen(col::bg);

        int cy = 48 + (1 - e) * 10, s = 46;
        uint16_t tile = mix(col::bg, accent(), e * 255);
        canvas.fillSmoothRoundRect(120 - s / 2, cy - s / 2, s, s, 13, tile);
        ui::drawIcon(ui::Icon::Power, 120, cy, 30, mix(tile, TFT_WHITE, e * 255), tile);

        canvas.setFont(font::big());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(mix(col::bg, col::text, e * 255));
        canvas.drawString(APP_NAME, 120, 80);
        canvas.setFont(font::small());
        canvas.setTextColor(mix(col::bg, col::muted, e * 255));
        canvas.drawString("Firmware launcher  v" APP_VERSION, 120, 104);
        ui::progressBar(90, 121, 60, 4, t, accent());
    }
};

class Welcome : public Screen {
    struct Row { const char* k1; Glyph g1; const char* k2; Glyph g2; const char* what; };
    static constexpr Row ROWS[] = {
        {";", Glyph::Up, ".", Glyph::Down, "Move up and down"},
        {",", Glyph::Left, "/", Glyph::Right, "Move left and right"},
        {"", Glyph::Enter, nullptr, Glyph::None, "Select / open"},
        {"esc", Glyph::None, "del", Glyph::None, "Go back"},
    };

public:
    void onKey(const KeyEvent& e) override {
        if (e.key != Key::Ok && e.key != Key::Back) return;
        if (settings::get().welcomed) {  // opened again from About
            nav::pop();
            return;
        }
        settings::get().welcomed = true;
        settings::save();
        nav::reset(screens::home());
    }
    void draw() override {
        canvas.fillScreen(col::bg);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString("Welcome to " APP_NAME, 120, 5);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString("Here's how to get around:", 120, 25);

        int y = 40;
        for (const Row& r : ROWS) {
            int x = 22;
            x += ui::keycap(x, y, 15, r.k1, r.g1) + 4;
            if (r.k2) ui::keycap(x, y, 15, r.k2, r.g2);
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_left);
            canvas.setTextColor(col::text);
            canvas.drawString(r.what, 92, y + 8);
            y += 19;
        }
        ui::footer({{"ok", settings::get().welcomed ? "got it" : "let's go!"}});
    }
};
constexpr Welcome::Row Welcome::ROWS[];

Splash splashScreen;
Welcome welcomeScreen;

}  // namespace

namespace screens {
Screen* splash() { return &splashScreen; }
Screen* welcome() { return &welcomeScreen; }
}
