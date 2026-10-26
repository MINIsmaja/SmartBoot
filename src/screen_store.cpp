// Firmware Store: Recommended / All tabs, search, details, install confirm/progress, app removal.
#include "catalog.h"
#include "apps.h"
#include "net.h"
#include "screens.h"

using ui::Glyph;
using ui::Icon;

namespace {

String sizeText(uint32_t bytes) {
    if (!bytes) return "";
    char b[16];
    snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0f);
    return b;
}

// ---- Firmware Store -------------------------------------------------------
class Store : public Screen {
    static constexpr int TAB_Y = 19, META_Y = 36, LIST_TOP = 54;
    static constexpr int LIST_HEIGHT = FOOTER_Y - LIST_TOP;
    int tab = 0;                 // 0 = Recommended, 1 = All
    int sel[2] = {0, 0};
    float scroll[2] = {0, 0};
    float pillX = -1, pillW = 0;
    int tabX[2] = {}, tabW[2] = {};
    bool caretOn = false;

    int count() const { return tab == 0 ? catalog::recommendedCount() : catalog::filteredCount(); }
    int rowH() const { return tab == 0 ? 30 : 21; }
    int totalH() const { return count() * rowH(); }

    void clampSel() { sel[tab] = count() ? constrain(sel[tab], 0, count() - 1) : 0; }

    float targetScroll() const {
        float s = scroll[tab];
        int top = sel[tab] * rowH(), bottom = top + rowH();
        if (top < s) s = top;
        if (bottom > s + LIST_HEIGHT) s = bottom - LIST_HEIGHT;
        return constrain(s, 0.0f, (float)max(0, totalH() - LIST_HEIGHT));
    }

    void refreshIfOnline() {
        if (net::state() == net::State::Online && !catalog::checkedOnline())
            nav::push(screens::updating(true));
    }

    void drawTabs() {
        canvas.setFont(font::body());
        char all[16];
        snprintf(all, sizeof(all), "All  %d", catalog::allCount());
        const char* labels[2] = {"Recommended", all};
        int x = 6;
        for (int i = 0; i < 2; i++) {
            tabX[i] = x;
            tabW[i] = canvas.textWidth(labels[i]) + (i == 0 ? 32 : 18);
            x += tabW[i] + 4;
        }
        if (pillX < 0) { pillX = tabX[tab]; pillW = tabW[tab]; }
        canvas.fillSmoothRoundRect(pillX, TAB_Y, pillW, 15, 7, accent());
        for (int i = 0; i < 2; i++) {
            int tx = tabX[i] + 9;
            if (i == 0) {
                ui::drawIcon(Icon::Star, tx + 3, TAB_Y + 7, 11, i == tab ? TFT_WHITE : col::warning, 0);
                tx += 14;
            }
            canvas.setTextDatum(middle_left);
            canvas.setTextColor(i == tab ? TFT_WHITE : col::muted);
            canvas.drawString(labels[i], tx, TAB_Y + 8);
        }
    }

    void drawMeta() {
        if (tab == 1) {  // search field
            canvas.fillSmoothRoundRect(4, META_Y, 232, 15, 7, col::surface);
            // magnifier
            int mx = 13, my = META_Y + 7;
            canvas.drawCircle(mx, my - 1, 3, col::muted);
            canvas.drawLine(mx + 2, my + 1, mx + 5, my + 4, col::muted);
            canvas.setFont(font::small());
            canvas.setTextDatum(middle_left);
            const String& q = catalog::query();
            if (q.length()) {
                canvas.setTextColor(col::text);
                canvas.drawString(q, 23, META_Y + 7);
                int cx = 23 + canvas.textWidth(q) + 1;  // blinking caret
                if (caretOn) canvas.drawFastVLine(cx, META_Y + 3, 9, accent());
                canvas.setTextDatum(middle_right);
                canvas.setTextColor(col::faint);
                char n[12];
                snprintf(n, sizeof(n), "%d", catalog::filteredCount());
                canvas.drawString(n, 231, META_Y + 7);
            } else {
                canvas.setTextColor(col::faint);
                canvas.drawString("Search firmware by name or author", 23, META_Y + 7);
            }
        } else {  // recommended: source / freshness line
            canvas.setFont(font::small());
            canvas.setTextDatum(middle_left);
            canvas.setTextColor(col::muted);
            if (net::state() != net::State::Online) {
                canvas.setTextColor(col::warning);
                canvas.drawString("Offline - connect to Wi-Fi to install", 6, META_Y + 7);
            } else {
                canvas.drawString("Top picks", 6, META_Y + 7);
                if (catalog::source() != catalog::Source::BuiltIn) {
                    canvas.setTextDatum(middle_right);
                    canvas.setTextColor(col::faint);
                    canvas.drawString(String("updated ") + catalog::updated(), 234, META_Y + 7);
                }
            }
        }
    }

    void rowFrame(int i, int y, int& x, int& w, int& h, bool& on) {
        on = i == sel[tab];
        x = 4; w = 228; h = rowH() - 3;
        canvas.fillSmoothRoundRect(x, y, w, h, 6, on ? col::surface2 : col::surface);
        if (on) canvas.drawRoundRect(x, y, w, h, 6, accent());
    }

    void drawRow(int i, int y) {
        int x, w, h; bool on;
        rowFrame(i, y, x, w, h, on);
        if (tab == 0) {  // Recommended: rich row with tagline
            const Firmware& fw = catalog::recommended(i);
            ui::firmwareTile(x + 4, y + 3, h - 6, fw.color, fw.name.c_str());
            canvas.setTextDatum(top_left);
            canvas.setFont(font::body());
            canvas.setTextColor(col::text);
            canvas.drawString(fw.name, x + 31, y + 2);
            canvas.setFont(font::small());
            canvas.setTextColor(col::muted);
            canvas.drawString(fw.tagline, x + 31, y + 15);
        } else {  // All: lean card (name + author)
            const FwCard& c = catalog::filtered(i);
            ui::firmwareTile(x + 3, y + 2, h - 4, c.color, c.name);
            canvas.setFont(font::small());
            String author = c.author;
            while (author.length() > 3 && canvas.textWidth(author) > 60) author.remove(author.length() - 1);
            if (author != c.author) author += "..";
            canvas.setTextColor(col::faint);
            canvas.setTextDatum(middle_right);
            canvas.drawString(author, x + w - 16, y + h / 2);
            int room = w - 16 - 24 - canvas.textWidth(author) - 8;  // between tile and author
            canvas.setFont(font::body());
            canvas.setTextColor(col::text);
            canvas.setTextDatum(middle_left);
            String name = c.name;
            while (name.length() > 3 && canvas.textWidth(name) > room) name.remove(name.length() - 1);
            if (name != c.name) name += "..";
            canvas.drawString(name, x + 24, y + h / 2);
        }
        ui::glyph(Glyph::Right, x + w - 7, y + h / 2, on ? accent() : col::faint);
    }

    // Drawn over the current frame while details download (the screen blocks for a moment).
    void showLoading() {
        ui::darken();
        canvas.fillSmoothRoundRect(55, 52, 130, 30, 8, col::surface2);
        canvas.setFont(font::body());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(col::text);
        canvas.drawString("Loading details...", 120, 67);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
    }

    void openSelected() {
        if (!count()) return;
        Firmware fw = tab == 0 ? catalog::recommended(sel[0])
                               : catalog::loadDetails(catalog::filtered(sel[1]));
        String err;
        installer::Check check;
        if (net::state() == net::State::Online) {
            showLoading();
            if (catalog::fetchDetails(fw, &err)) check = installer::check(fw);
        }
        nav::push(screens::details(fw, check));
        if (err.length()) ui::toast(err.c_str(), col::danger);
    }

    void typeSearch(char c) {
        if (c < 32 || c > 126) return;
        String q = catalog::query();
        if (q.length() < 24) {
            q += c;
            catalog::setQuery(q);
            sel[1] = 0;
            scroll[1] = 0;
        }
    }

public:
    void onEnter() override {
        tab = 0;  // fresh open starts on Recommended
        clampSel();
        refreshIfOnline();
    }

    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: sel[tab] = max(0, sel[tab] - 1); break;
            case Key::Down: sel[tab] = min(count() - 1, sel[tab] + 1); break;
            case Key::Left: tab = 0; clampSel(); break;
            case Key::Right: tab = 1; clampSel(); break;
            case Key::Tab: tab = 1 - tab; clampSel(); break;
            case Key::Ok: openSelected(); break;
            case Key::Char:
                if (tab == 1) typeSearch(e.ch);
                break;
            case Key::Back:
                if (tab == 1 && catalog::query().length()) {
                    String q = catalog::query();
                    q.remove(q.length() - 1);
                    catalog::setQuery(q);
                    clampSel();
                } else {
                    nav::pop();
                }
                break;
            default:
                break;
        }
    }

    bool update() override {
        bool moving = ui::approach(scroll[tab], targetScroll());
        moving |= ui::approach(pillX, tabX[tab]);
        moving |= ui::approach(pillW, tabW[tab]);
        if (tab == 1 && catalog::query().length()) {  // redraw only when the caret blinks
            bool on = (millis() / 500) % 2;
            if (on != caretOn) { caretOn = on; moving = true; }
        }
        return moving;
    }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Firmware Store");
        drawTabs();
        drawMeta();

        canvas.setClipRect(0, LIST_TOP, SCREEN_W, LIST_HEIGHT);
        int y0 = LIST_TOP - (int)scroll[tab];
        if (count() == 0) {
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_center);
            canvas.setTextColor(col::faint);
            const char* empty = tab == 0                        ? "Nothing here yet"
                                : catalog::query().length()     ? "No matches"
                                                                : "Connect to Wi-Fi to load the list";
            canvas.drawString(empty, 120, LIST_TOP + LIST_HEIGHT / 2);
        }
        for (int i = 0; i < count(); i++) {
            int y = y0 + i * rowH();
            if (y + rowH() < LIST_TOP || y > FOOTER_Y) continue;
            drawRow(i, y);
        }
        canvas.clearClipRect();
        ui::scrollbar(235, LIST_TOP, LIST_HEIGHT - 2, scroll[tab], LIST_HEIGHT, totalH());

        if (tab == 1)
            ui::footer({{"ud", "move"}, {"A-Z", "find"}, {"ok", "open"}, {"esc", "back"}});
        else
            ui::footer({{"ud", "move"}, {"tab", "tabs"}, {"ok", "open"}, {"esc", "back"}});
    }
};

// ---- Details --------------------------------------------------------------
class Details : public Screen {
    static constexpr int BODY_Y = 50, BODY_H = FOOTER_Y - BODY_Y - 1;
    Firmware fw;
    installer::Check check;
    float scroll = 0;
    int target = 0, contentH = 0;

    int maxScroll() const { return max(0, contentH - BODY_H); }
    bool installable() const { return check.done && check.plan.ok; }

    int drawTags(int x, int y) {
        canvas.setFont(font::small());
        String tags = fw.good;
        int startX = x;
        while (tags.length()) {
            int bar = tags.indexOf('|');
            String tag = bar < 0 ? tags : tags.substring(0, bar);
            tags = bar < 0 ? "" : tags.substring(bar + 1);
            if (!tag.length()) continue;
            int w = canvas.textWidth(tag) + 12;
            if (x + w > 232) { x = startX; y += 17; }
            canvas.fillSmoothRoundRect(x, y, w, 14, 7, col::surface2);
            canvas.setTextDatum(middle_center);
            canvas.setTextColor(col::text);
            canvas.drawString(tag, x + w / 2, y + 7);
            x += w + 4;
        }
        return y + 17;
    }

    int infoRow(int y, const char* label, const String& value) {
        if (!value.length()) return y;
        canvas.setFont(font::small());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::muted);
        canvas.drawString(label, 8, y);
        canvas.setTextDatum(top_right);
        canvas.setTextColor(col::text);
        canvas.drawString(value, 232, y);
        return y + 14;
    }

    // A coloured note with a bar on its left; returns the y below it.
    int notice(int y, uint16_t color, const String& text) {
        canvas.setFont(font::small());
        int h = ui::wrapText(text.c_str(), 16, y, 214, 11, color);
        canvas.fillSmoothRoundRect(8, y, 3, h, 1, color);
        return y + h + 6;
    }

    // What installing would do, or why it can't; empty when there is nothing to say.
    String installNote(uint16_t& color) const {
        const installer::Plan& p = check.plan;
        if (!check.done) {
            color = col::warning;
            return check.error.length() ? "Couldn't check the install: " + check.error : String();
        }
        if (!p.ok) {
            color = col::danger;
            return "Can't install: " + p.reason;
        }
        color = col::warning;
        if (!p.removes.empty()) {
            String names;
            for (size_t i = 0; i < p.removes.size(); i++)
                names += (i ? (i + 1 == p.removes.size() ? " and " : ", ") : "") + p.removes[i].name;
            return "To make room, installing removes " + names + ".";
        }
        if (!p.shrinks.empty()) return "To make room, " + p.shrinks[0] + ".";
        color = col::success;
        if (p.replaces)
            return p.old.version == fw.version ? String("Installed. Installing again keeps its saved data.")
                                               : "Installed (" + p.old.version + "). Updating keeps its saved data.";
        return "";
    }

public:
    void set(const Firmware& f, const installer::Check& c) {
        fw = f;
        check = c;
        scroll = target = contentH = 0;
    }

    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Up: target = max(0, target - 16); break;
            case Key::Down: target = min(maxScroll(), target + 16); break;
            case Key::Ok:
                if (installable()) nav::push(screens::confirmInstall(fw, check));
                else if (net::state() != net::State::Online) ui::toast("Connect to Wi-Fi first", col::warning);
                else if (check.done) ui::toast("This one can't be installed", col::warning);
                else ui::toast("Couldn't load the install info", col::warning);
                break;
            case Key::Back: nav::pop(); break;
            default: break;
        }
    }
    bool update() override { return ui::approach(scroll, target); }

    void draw() override {
        canvas.fillScreen(col::bg);
        ui::statusBar("Details");

        ui::firmwareTile(8, 20, 26, fw.color, fw.name.c_str());
        canvas.setFont(font::title());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::text);
        String title = fw.name;
        while (title.length() > 3 && canvas.textWidth(title) > 190) title.remove(title.length() - 1);
        if (title != fw.name) title += "..";
        canvas.drawString(title, 42, 20);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString(fw.author.length() ? "by " + fw.author : String("unknown author"), 42, 38);
        canvas.drawFastHLine(8, BODY_Y - 2, 224, col::surface2);

        canvas.setClipRect(0, BODY_Y, SCREEN_W, BODY_H);
        int y = BODY_Y + 3 - (int)scroll;
        uint16_t noteColor = col::muted;
        String note = installNote(noteColor);
        if (note.length()) y = notice(y, noteColor, note);

        canvas.setFont(font::body());
        const String& blurb = fw.about.length() ? fw.about : fw.tagline;
        if (blurb.length()) y += ui::wrapText(blurb.c_str(), 8, y, 222, 13, col::text) + 6;

        if (fw.good.length()) {
            canvas.setFont(font::small());
            canvas.setTextDatum(top_left);
            canvas.setTextColor(col::muted);
            canvas.drawString("GOOD FOR", 8, y);
            y = drawTags(8, y + 12) + 6;
        }

        if (blurb.length() || fw.good.length()) {  // separate the text from the info rows
            canvas.drawFastHLine(8, y, 224, col::surface2);
            y += 6;
        }
        if (fw.version.length() || check.manifest.sdPath.length()) {  // SD files may not name a version
            y = infoRow(y, "Version", fw.version);
            y = infoRow(y, "Size", sizeText(fw.size));
            if (installable()) {  // its own storage (saves, files, games), as planned
                uint32_t storage = 0;
                for (const String& label : check.plan.dataLabels)
                    if (const parts::Entry* e = parts::find(check.plan.table, label.c_str())) storage += e->size;
                y = infoRow(y, "Its storage", sizeText(storage));
            }
            y = infoRow(y, "Published", fw.date);
        } else {
            canvas.setFont(font::small());
            canvas.setTextDatum(top_left);
            canvas.setTextColor(col::muted);
            canvas.drawString(net::state() == net::State::Online ? "Couldn't load version and size"
                                                                 : "Connect to Wi-Fi for version and size",
                              8, y);
            y += 14;
        }
        y += 2;
        contentH = y + (int)scroll - BODY_Y;
        canvas.clearClipRect();

        ui::scrollbar(235, BODY_Y, BODY_H - 2, scroll, BODY_H, contentH);
        if (check.done && !check.plan.ok)
            ui::footer({{"ud", "scroll"}, {"esc", "back"}});
        else
            ui::footer({{"ok", check.plan.replaces ? "update" : "install"}, {"ud", "scroll"}, {"esc", "back"}});
    }
};

// ---- Confirm dialogs --------------------------------------------------------
// A two-button dialog drawn over the screen below; choice 1 is the action.
class Dialog : public Screen {
protected:
    int choice = 1;
    virtual const char* actionLabel() const = 0;
    virtual void act() = 0;
    // A short title, the app's name on its own line, then the body (its color can warn).
    void frame(const char* title, const String& name, const String& body, uint16_t bodyColor) {
        const int x = 12, y = 19, w = 216, h = 98;
        canvas.fillSmoothRoundRect(x, y, w, h, 10, col::surface);
        canvas.drawRoundRect(x, y, w, h, 10, col::line);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_left);
        canvas.setTextColor(col::text);
        canvas.drawString(title, x + 12, y + 7);
        canvas.setFont(font::body());
        canvas.setTextColor(accent());
        String n = name;
        while (n.length() > 3 && canvas.textWidth(n) > w - 24) n.remove(n.length() - 1);
        if (n != name) n += "..";
        canvas.drawString(n, x + 12, y + 25);
        canvas.setFont(font::small());
        ui::wrapText(body.c_str(), x + 12, y + 40, w - 24, 11, bodyColor);

        const char* labels[2] = {"Cancel", actionLabel()};
        for (int i = 0; i < 2; i++) {
            int bx = x + 12 + i * 100, by = y + h - 26, bw = 92, bh = 19;
            bool on = i == choice;
            canvas.fillSmoothRoundRect(bx, by, bw, bh, 9, on ? accent() : col::surface2);
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_center);
            canvas.setTextColor(on ? TFT_WHITE : col::muted);
            canvas.drawString(labels[i], bx + bw / 2, by + bh / 2 + 1);
        }
    }

public:
    bool isOverlay() const override { return true; }
    void onKey(const KeyEvent& e) override {
        switch (e.key) {
            case Key::Left: choice = 0; break;
            case Key::Right: choice = 1; break;
            case Key::Tab: choice = 1 - choice; break;
            case Key::Back: nav::pop(); break;
            case Key::Ok:
                nav::pop();
                if (choice == 1) act();
                break;
            default: break;
        }
    }
};

class ConfirmInstall : public Dialog {
    Firmware fw;
    installer::Check check;

    const char* actionLabel() const override { return check.plan.replaces ? "Update" : "Install"; }
    void act() override {
        int battery = M5.Power.getBatteryLevel();
        bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
        if (battery >= 0 && battery < 15 && !charging) {
            ui::toast("Battery low - plug in USB first", col::warning);
        } else if (check.manifest.sdPath.isEmpty() && net::state() != net::State::Online) {
            ui::toast("Connect to Wi-Fi first", col::warning);
        } else {
            nav::push(screens::install(fw, check));
        }
    }

public:
    void set(const Firmware& f, const installer::Check& c) {
        fw = f;
        check = c;
        choice = 1;
    }

    void draw() override {
        const installer::Plan& p = check.plan;
        String body;
        uint16_t color = col::muted;
        if (!p.removes.empty()) {
            body = "To make room, this removes ";
            for (size_t i = 0; i < p.removes.size(); i++)
                body += (i ? (i + 1 == p.removes.size() ? " and " : ", ") : "") + p.removes[i].name;
            body += p.removes.size() == 1 ? ", with its saved data." : ", with their saved data.";
            color = col::warning;
        } else if (!p.shrinks.empty()) {
            body = "To make room, " + p.shrinks[0] + ".";
            color = col::warning;
        } else if (p.replaces) {
            body = "Its saved data stays.";
        } else {
            body = "Turn the Cardputer off and on to come back to " APP_NAME ".";
        }
        frame(p.replaces ? "Update this app?" : "Install this app?", fw.name, body, color);
    }
};

class ConfirmRemove : public Dialog {
    App app;
    const char* actionLabel() const override { return "Remove"; }
    void act() override {
        canvas.fillScreen(col::bg);  // freeing space can take a few seconds
        canvas.setFont(font::title());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(col::text);
        canvas.drawString("Removing...", 120, 67);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
        String err;
        if (apps::remove(app, &err)) ui::toast((app.name + " removed").c_str(), col::success);
        else ui::toast(err.c_str(), col::danger);
    }

public:
    void set(const App& a) {
        app = a;
        choice = 0;  // the safe choice first
    }
    void draw() override {
        frame("Remove this app?", app.name,
              "Its saved data goes too. You can install it again from the store.", col::muted);
    }
};

// ---- Install ------------------------------------------------------------------
class Install : public Screen {
    Firmware fw;
    installer::Check check;
    installer::Result result{false, ""};
    bool done = false;
    uint32_t doneAt = 0;
    int choice = 0;  // after success: 0 = open now, 1 = done
    String stage;
    int pct = -1;

    // Drawn straight to the display from the install callback, which blocks the loop.
    void progressFrame(const char* s, int p) {
        if (stage == s && p == pct) return;  // called per download chunk; redraw on change only
        stage = s;
        pct = p;
        canvas.fillScreen(col::bg);
        ui::statusBar("Installing");
        ui::firmwareTile(102, 24, 36, fw.color, fw.name.c_str());
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        String t = fw.name;
        while (t.length() > 3 && canvas.textWidth(t) > 224) t.remove(t.length() - 1);
        canvas.drawString(t, 120, 70);
        ui::progressBar(30, 93, 180, 8, pct / 100.0f, accent());
        char line[48];
        snprintf(line, sizeof(line), "%s...  %d%%", s, pct);
        canvas.setFont(font::small());
        canvas.setTextColor(col::muted);
        canvas.drawString(line, 120, 106);
        canvas.drawString("Keep the Cardputer on", 120, 120);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
    }

    void finish() {
        nav::pop();  // Install
        nav::pop();  // Details, which still shows the old plan
        ui::toast(result.message.c_str(), result.ok ? col::success : col::danger);
    }

    void openNow() {
        const App* a = apps::byId(fw.id);
        if (!a) return finish();
        canvas.fillScreen(col::bg);
        canvas.setFont(font::title());
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(col::text);
        canvas.drawString(String("Starting ") + a->name + "...", 120, 67);
        canvas.pushSprite(&M5Cardputer.Display, 0, 0);
        String err;
        apps::launch(*a, &err);  // restarts on success
        result = {false, err};
        finish();
    }

public:
    void set(const Firmware& f, const installer::Check& c) {
        fw = f;
        check = c;
    }

    void onEnter() override {
        done = false;
        choice = 0;
        stage = "";
        pct = -1;
        progressFrame("Starting", 0);
        result = installer::install(fw, check.manifest, check.plan,
                                    [&](const char* s, int p) { progressFrame(s, p); });
        done = true;
        doneAt = millis();
    }

    void onKey(const KeyEvent& e) override {
        // Ignore keys pressed while installing, so a stray Enter doesn't start the app.
        if (!done || millis() - doneAt < 800) return;
        if (!result.ok) {
            if (e.key == Key::Ok || e.key == Key::Back) finish();
            return;
        }
        switch (e.key) {
            case Key::Left: choice = 0; break;
            case Key::Right: choice = 1; break;
            case Key::Tab: choice = 1 - choice; break;
            case Key::Back: finish(); break;
            case Key::Ok:
                if (choice == 0) openNow();
                else finish();
                break;
            default: break;
        }
    }

    void draw() override {
        if (!done) return;
        canvas.fillScreen(col::bg);
        ui::statusBar(result.ok ? "Installed" : "Install failed");
        ui::drawIcon(result.ok ? Icon::Check : Icon::Info, 120, 42, 40, result.ok ? col::success : col::danger,
                     col::bg);
        canvas.setFont(font::title());
        canvas.setTextDatum(top_center);
        canvas.setTextColor(col::text);
        canvas.drawString(result.ok ? "All done!" : "Couldn't install", 120, 66);
        canvas.setFont(font::small());
        if (!result.ok) {
            ui::wrapText(result.message.c_str(), 16, 86, 208, 11, col::muted, true);
            ui::footer({{"ok", "continue"}});
            return;
        }
        canvas.setTextColor(col::muted);
        canvas.drawString("It's on your Home screen.", 120, 84);
        const char* labels[2] = {"Open now", "Done"};
        for (int i = 0; i < 2; i++) {
            int bx = 24 + i * 100, by = 98, bw = 92, bh = 19;
            bool on = i == choice;
            canvas.fillSmoothRoundRect(bx, by, bw, bh, 9, on ? accent() : col::surface2);
            canvas.setFont(font::body());
            canvas.setTextDatum(middle_center);
            canvas.setTextColor(on ? TFT_WHITE : col::muted);
            canvas.drawString(labels[i], bx + bw / 2, by + bh / 2 + 1);
        }
        ui::footer({{"lr", "choose"}, {"ok", "select"}});
    }
};

Store storeScreen;
Details detailsScreen;
ConfirmInstall confirmScreen;
ConfirmRemove removeScreen;
Install installScreen;

}  // namespace

namespace screens {
Screen* store() { return &storeScreen; }
Screen* details(const Firmware& f, const installer::Check& c) { detailsScreen.set(f, c); return &detailsScreen; }
Screen* confirmInstall(const Firmware& f, const installer::Check& c) { confirmScreen.set(f, c); return &confirmScreen; }
Screen* install(const Firmware& f, const installer::Check& c) { installScreen.set(f, c); return &installScreen; }
Screen* confirmRemove(const App& a) { removeScreen.set(a); return &removeScreen; }
}
