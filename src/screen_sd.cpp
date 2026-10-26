// SD Card: a light file browser for installing .bin firmware from the microSD card.
// Shows only folders and .bin files (tab shows everything), folders first, A-Z.
// Picking a .bin opens the same Details page as the store, with the install plan.
#include "sdcard.h"
#include "screens.h"

using ui::Glyph;
using ui::Icon;

namespace {

String sizeText(uint32_t bytes) {
    char b[16];
    if (bytes >= 1048576) snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0f);
    else snprintf(b, sizeof(b), "%u KB", (unsigned)((bytes + 1023) / 1024));
    return b;
}

String parentOf(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash <= 0 ? String("/") : path.substring(0, slash);
}

String join(const String& dir, const String& name) { return dir == "/" ? "/" + name : dir + "/" + name; }

class SdBrowser : public Screen {
    static constexpr int PATH_Y = 19, LIST_TOP = 34, ROW_H = 21;
    static constexpr int LIST_HEIGHT = FOOTER_Y - LIST_TOP;
    String path = "/";  // remembered while SmartBoot runs
    sdcard::Listing listing;
    bool cardOk = false;
    bool allFiles = false;
    int sel = 0;
    float scroll = 0;

    int count() const { return listing.entries.size(); }

    // Drawn over the current frame while the card is read (folders with many files).
    void busy(const char* text) {
        ui::darken();
        canvas.fillSmoothRoundRect(55, 52, 130, 30, 8, col::surface2);
        canvas.setFont(font::body());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(col::text);
        canvas.drawString(text, 120, 67);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
    }

    // Loads `path`; selects the entry named `select` if given.
    void load(const String& select = "") {
        cardOk = sdcard::mount();
        if (!cardOk) return;
        busy("Reading card...");
        if (!sdcard::list(path, allFiles, listing) && path != "/") {  // folder gone (card swapped)
            path = "/";
            sdcard::list(path, allFiles, listing);
        }
        sel = 0;
        for (int i = 0; i < count(); i++)
            if (listing.entries[i].name == select) sel = i;
        scroll = targetScroll();
    }

    float targetScroll() const {
        float s = scroll;
        int top = sel * ROW_H, bottom = top + ROW_H;
        if (top < s) s = top;
        if (bottom > s + LIST_HEIGHT) s = bottom - LIST_HEIGHT;
        return constrain(s, 0.0f, (float)max(0, count() * ROW_H - LIST_HEIGHT));
    }

    void open() {
        if (!count()) return;
        const sdcard::Entry& e = listing.entries[sel];
        if (e.dir) {
            path = join(path, e.name);
            load();
            return;
        }
        if (!e.name.endsWith(".bin") && !e.name.endsWith(".BIN")) {
            ui::toast("Only .bin files can be installed", col::warning);
            return;
        }
        busy("Reading file...");
        Firmware fw;
        installer::Check check = sdcard::inspect(join(path, e.name), fw);
        nav::push(screens::details(fw, check));
    }

    void up() {
        if (path == "/") {
            sdcard::unmount();
            nav::pop();
            return;
        }
        String from = path.substring(path.lastIndexOf('/') + 1);
        path = parentOf(path);
        load(from);
    }

    // Next entry (after the selection, wrapping) whose name starts with c.
    void jumpTo(char c) {
        for (int k = 1; k <= count(); k++) {
            int i = (sel + k) % count();
            if (tolower(listing.entries[i].name[0]) == tolower(c)) {
                sel = i;
                return;
            }
        }
    }

    void drawRow(int i, int y) {
        const sdcard::Entry& e = listing.entries[i];
        bool on = i == sel;
        int x = 4, w = 228, h = ROW_H - 3;
        canvas.fillSmoothRoundRect(x, y, w, h, 6, on ? col::surface2 : col::surface);
        if (on) canvas.drawRoundRect(x, y, w, h, 6, accent());
        int ix = x + 4, iy = y + 3;
        if (e.dir) {  // folder: tab + body
            uint16_t c = rgb(0xFFB547);
            canvas.fillSmoothRoundRect(ix, iy + 1, 7, 4, 1, c);
            canvas.fillSmoothRoundRect(ix, iy + 3, 15, 10, 2, c);
        } else {
            String base = e.name.endsWith(".bin") ? e.name.substring(0, e.name.length() - 4) : e.name;
            ui::firmwareTile(ix, iy, h - 6, catalog::tileColor(base.c_str()), e.name.c_str());  // same color as Details
        }
        String right = e.dir ? String() : sizeText(e.size);
        canvas.setFont(font::small());
        int rightW = right.length() ? canvas.textWidth(right) + 8 : 0;
        canvas.setTextColor(col::faint);
        canvas.setTextDatum(middle_right);
        if (right.length()) canvas.drawString(right, x + w - 16, y + h / 2);
        canvas.setFont(font::body());
        canvas.setTextColor(col::text);
        canvas.setTextDatum(middle_left);
        String name = e.name;
        int room = w - 16 - 24 - rightW;
        while (name.length() > 3 && canvas.textWidth(name) > room) name.remove(name.length() - 1);
        if (name != e.name) name += "..";
        canvas.drawString(name, x + 24, y + h / 2);
        ui::glyph(e.dir ? Glyph::Right : Glyph::Enter, x + w - 7, y + h / 2, on ? accent() : col::faint);
    }

    void drawMessage(const char* title, const String& line) {
        ui::drawIcon(Icon::SdCard, 120, 58, 34, col::faint, col::bg);
        canvas.setFont(font::body());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString(title, 120, 80);
        canvas.setFont(font::small());
        ui::wrapText(line.c_str(), 16, 96, 208, 11, col::muted, true);
    }

public:
    void onEnter() override {
        allFiles = false;
        load();
    }

    void onKey(const KeyEvent& e) override {
        if (!cardOk) {
            if (e.key == Key::Ok) load();
            else if (e.key == Key::Back) nav::pop();
            return;
        }
        switch (e.key) {
            case Key::Up: sel = max(0, sel - 1); break;
            case Key::Down: sel = min(count() - 1, sel + 1); break;
            case Key::Ok: case Key::Right: open(); break;
            case Key::Back: case Key::Left: up(); break;
            case Key::Tab: {
                allFiles = !allFiles;
                String keep = count() ? listing.entries[sel].name : String();
                load(keep);
                ui::toast(allFiles ? "Showing all files" : "Showing firmware only");
                break;
            }
            case Key::Char: jumpTo(e.ch); break;
            default: break;
        }
    }

    bool update() override { return ui::approach(scroll, targetScroll()); }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("SD Card");
        if (!cardOk) {
            drawMessage("No SD card found", "Insert a microSD card (FAT32), then press OK.");
            ui::footer({{"ok", "retry"}, {"esc", "back"}});
            return;
        }
        // Where we are; long paths keep their end.
        canvas.setFont(font::small());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::muted);
        String shown = path;
        while (shown.length() > 4 && canvas.textWidth(shown) > 200) shown = ".." + shown.substring(3);
        canvas.drawString(shown, 6, PATH_Y + 6);
        if (listing.hidden && !allFiles) {
            canvas.setTextDatum(middle_right);
            canvas.setTextColor(col::faint);
            canvas.drawString(String(listing.hidden) + " hidden", 234, PATH_Y + 6);
        }

        if (!count()) {
            String hint = listing.hidden ? String(listing.hidden) + " other files here. Tab shows them."
                                         : String("This folder is empty.");
            drawMessage(allFiles ? "Empty folder" : "No firmware here", hint);
        }
        canvas.setClipRect(0, LIST_TOP, SCREEN_W, LIST_HEIGHT);
        for (int i = 0; i < count(); i++) {
            int y = LIST_TOP + i * ROW_H - (int)scroll;
            if (y + ROW_H < LIST_TOP || y > FOOTER_Y) continue;
            drawRow(i, y);
        }
        canvas.clearClipRect();
        ui::scrollbar(235, LIST_TOP, LIST_HEIGHT - 2, scroll, LIST_HEIGHT, count() * ROW_H);
        ui::footer({{"ok", "open"}, {"tab", allFiles ? "bin only" : "all files"},
                    {"esc", path == "/" ? "back" : "up"}});
    }
};

SdBrowser sdScreen;

}  // namespace

namespace screens {
Screen* sdBrowser() { return &sdScreen; }
}
