#include "ui.h"
#include "net.h"

M5Canvas canvas;  // no parent here: M5Cardputer may not be constructed yet

namespace {
struct {
    char msg[48] = "";
    uint16_t color = col::text;
    uint32_t until = 0;
} toastState;

int battery = -1;
bool charging = false;
uint32_t batteryReadAt = 0;

void readBattery() {
    if (batteryReadAt && millis() - batteryReadAt < 5000) return;
    batteryReadAt = millis();
    battery = M5.Power.getBatteryLevel();
    charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
}

void drawGlyph(ui::Glyph g, int cx, int cy, uint16_t c) {
    using ui::Glyph;
    switch (g) {
        case Glyph::Up:    canvas.fillTriangle(cx - 3, cy + 2, cx + 3, cy + 2, cx, cy - 2, c); break;
        case Glyph::Down:  canvas.fillTriangle(cx - 3, cy - 2, cx + 3, cy - 2, cx, cy + 2, c); break;
        case Glyph::Left:  canvas.fillTriangle(cx + 2, cy - 3, cx + 2, cy + 3, cx - 2, cy, c); break;
        case Glyph::Right: canvas.fillTriangle(cx - 2, cy - 3, cx - 2, cy + 3, cx + 2, cy, c); break;
        case Glyph::Enter:
            canvas.drawFastVLine(cx + 3, cy - 3, 4, c);
            canvas.drawFastHLine(cx - 2, cy + 1, 5, c);
            canvas.fillTriangle(cx - 4, cy + 1, cx - 1, cy - 2, cx - 1, cy + 4, c);
            break;
        default: break;
    }
}

void star(int cx, int cy, float r, uint16_t c) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float a = -PI / 2 + i * PI / 5;
        float rr = (i % 2) ? r * 0.45f : r;
        px[i] = cx + cosf(a) * rr;
        py[i] = cy + sinf(a) * rr;
    }
    for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        canvas.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], c);
    }
}
}  // namespace

namespace ui {

void drawIcon(Icon icon, int cx, int cy, int s, uint16_t c, uint16_t bg) {
    switch (icon) {
        case Icon::Store: {  // arrow dropping into a tray
            int shaft = max(2, s / 7);
            canvas.fillRect(cx - shaft / 2, cy - s * 0.40f, shaft, s * 0.42f, c);
            canvas.fillTriangle(cx - s * 0.24f, cy - s * 0.04f, cx + s * 0.24f, cy - s * 0.04f, cx, cy + s * 0.20f, c);
            int t = max(2, s / 9);
            int left = cx - s * 0.40f, right = cx + s * 0.40f, bottom = cy + s * 0.38f;
            canvas.fillRect(left, cy + s * 0.10f, t, bottom - (cy + s * 0.10f), c);
            canvas.fillRect(right - t, cy + s * 0.10f, t, bottom - (cy + s * 0.10f), c);
            canvas.fillRect(left, bottom - t, right - left, t, c);
            break;
        }
        case Icon::SdCard: {
            int w = s * 0.60f, h = s * 0.80f, x = cx - w / 2, y = cy - h / 2, cut = s * 0.20f;
            canvas.fillRoundRect(x, y, w, h, max(1, s / 12), c);
            canvas.fillTriangle(x + w - cut, y - 1, x + w + 1, y - 1, x + w + 1, y + cut, bg);
            int pinW = max(1, s / 14), gap = (w - cut) / 4;
            for (int i = 0; i < 3; i++)
                canvas.fillRect(x + gap * (i + 1) - pinW / 2, y + h * 0.12f, pinW, h * 0.22f, bg);
            break;
        }
        case Icon::Wifi: {
            int oy = cy + s * 0.30f;
            float t = max(2.0f, s * 0.10f);
            for (float r : {s * 0.62f, s * 0.42f}) canvas.fillArc(cx, oy, r - t, r, 225, 315, c);
            canvas.fillCircle(cx, oy - s * 0.04f, max(2.0f, s * 0.12f), c);
            break;
        }
        case Icon::Gear: {
            float ro = s * 0.44f, rb = s * 0.31f;
            for (int i = 0; i < 8; i++) canvas.fillArc(cx, cy, rb - 1, ro, i * 45 - 11, i * 45 + 11, c);
            canvas.fillCircle(cx, cy, rb, c);
            canvas.fillCircle(cx, cy, s * 0.13f, bg);
            break;
        }
        case Icon::Info: {
            canvas.fillCircle(cx, cy, s * 0.42f, c);
            int w = max(2.0f, s * 0.11f);
            canvas.fillCircle(cx, cy - s * 0.19f, max(1.0f, s * 0.065f), bg);
            canvas.fillRect(cx - w / 2, cy - s * 0.06f, w, s * 0.29f, bg);
            break;
        }
        case Icon::Star: star(cx, cy, s * 0.48f, c); break;
        case Icon::Check: {
            canvas.fillCircle(cx, cy, s * 0.46f, c);
            float w = max(1.5f, s * 0.07f);
            canvas.drawWideLine(cx - s * 0.20f, cy + s * 0.01f, cx - s * 0.05f, cy + s * 0.16f, w, bg);
            canvas.drawWideLine(cx - s * 0.05f, cy + s * 0.16f, cx + s * 0.22f, cy - s * 0.14f, w, bg);
            break;
        }
        case Icon::Transfer: {  // two arrows, one each way
            int t = max(2, s / 9);
            float half = s * 0.38f, head = s * 0.20f, dy = s * 0.17f;
            for (int dir = -1; dir <= 1; dir += 2) {
                float y = cy - dir * dy, tip = cx + dir * half, tail = cx - dir * half;
                canvas.fillRect(min(tip - dir * head, tail), y - t / 2.0f, fabsf(tip - dir * head - tail) + 1, t, c);
                canvas.fillTriangle(tip, y, tip - dir * head, y - head * 0.75f, tip - dir * head, y + head * 0.75f, c);
            }
            break;
        }
        case Icon::Power: {
            float r = s * 0.38f, t = max(2.0f, s * 0.10f);
            canvas.fillArc(cx, cy + s * 0.04f, r - t, r, 300, 360, c);
            canvas.fillArc(cx, cy + s * 0.04f, r - t, r, 0, 240, c);
            canvas.fillRoundRect(cx - t / 2, cy - s * 0.42f, t, s * 0.42f, t / 2, c);
            break;
        }
    }
}

void statusBar(const char* title) {
    readBattery();
    canvas.fillRect(0, 0, SCREEN_W, STATUS_H, col::bg);
    canvas.drawFastHLine(0, STATUS_H - 1, SCREEN_W, col::surface2);
    canvas.fillCircle(7, 7, 3, accent());
    canvas.setFont(font::body());
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col::text);
    canvas.drawString(title, 15, 8);

    // Battery: outline, nub, fill level, then the percentage to its left
    int bx = SCREEN_W - 24, by = 4;
    canvas.drawRoundRect(bx, by, 18, 9, 2, col::muted);
    canvas.fillRect(bx + 18, by + 3, 2, 3, col::muted);
    int x = bx - 4;
    if (battery >= 0) {
        uint16_t c = charging ? col::success : battery > 50 ? col::success : battery > 20 ? col::warning : col::danger;
        canvas.fillRect(bx + 2, by + 2, max(1, 14 * battery / 100), 5, c);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", battery);
        canvas.setFont(font::small());
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(col::muted);
        canvas.drawString(pct, x, 8);
        x -= canvas.textWidth(pct) + 6;
    }
    // Wi-Fi: bright when online (brightness by signal), amber connecting, grey off
    int wx = x - 7;
    net::State ws = net::state();
    int wb = net::bars();
    uint16_t wc = ws == net::State::Online ? accent()
                  : ws == net::State::Connecting ? col::warning : col::faint;
    canvas.fillArc(wx, 12, 6, 7, 225, 315, ws == net::State::Online && wb >= 3 ? wc : col::surface2);
    canvas.fillArc(wx, 12, 3, 4, 225, 315, ws == net::State::Online && wb >= 1 ? wc : (ws == net::State::Off ? col::surface2 : wc));
    canvas.fillCircle(wx, 11, 1, wc);
}

int keycap(int x, int y, int h, const char* label, Glyph g1, Glyph g2) {
    canvas.setFont(font::small());
    int textW = (label && *label) ? canvas.textWidth(label) : 0;
    int glyphs = (g1 != Glyph::None) + (g2 != Glyph::None);
    int w = 6 + textW + glyphs * 9 + (textW && glyphs ? 2 : 0);
    canvas.fillRoundRect(x, y, w, h, 3, col::surface2);
    canvas.drawFastHLine(x + 2, y + h - 1, w - 4, col::line);  // key "depth"
    int cx = x + 3;
    if (textW) {
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::text);
        canvas.drawString(label, cx, y + h / 2);
        cx += textW + 2;
    }
    if (g1 != Glyph::None) { drawGlyph(g1, cx + 4, y + h / 2, col::text); cx += 9; }
    if (g2 != Glyph::None) { drawGlyph(g2, cx + 4, y + h / 2, col::text); }
    return w;
}

void footer(std::initializer_list<Hint> hints) {
    canvas.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, col::surface);
    int x = 4, y = FOOTER_Y + 2;
    for (const Hint& h : hints) {
        int w;
        if (!strcmp(h.key, "ud"))      w = keycap(x, y, 10, "", Glyph::Up, Glyph::Down);
        else if (!strcmp(h.key, "lr")) w = keycap(x, y, 10, "", Glyph::Left, Glyph::Right);
        else if (!strcmp(h.key, "ok")) w = keycap(x, y, 10, "", Glyph::Enter);
        else                           w = keycap(x, y, 10, h.key);
        x += w + 2;
        canvas.setFont(font::small());
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(col::muted);
        canvas.drawString(h.action, x, y + 5);
        x += canvas.textWidth(h.action) + 7;
    }
}

void glyph(Glyph g, int cx, int cy, uint16_t color) { drawGlyph(g, cx, cy, color); }

void toggle(int x, int y, bool on) {
    canvas.fillSmoothRoundRect(x, y, 24, 12, 6, on ? accent() : col::surface2);
    canvas.fillSmoothCircle(on ? x + 17 : x + 6, y + 6, 4, on ? TFT_WHITE : col::muted);
}

void firmwareTile(int x, int y, int size, uint32_t rgb888, const char* name) {
    uint16_t c = rgb(rgb888);
    canvas.fillSmoothRoundRect(x, y, size, size, size / 4, c);
    char letter[2] = {(char)toupper(name[0]), 0};
    canvas.setFont(size >= 24 ? font::title() : font::body());
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(letter, x + size / 2, y + size / 2 + 1);
}

int wrapText(const char* text, int x, int y, int w, int lineH, uint16_t color, bool center) {
    canvas.setTextDatum(center ? top_center : top_left);
    if (center) x += w / 2;
    canvas.setTextColor(color);
    String line, word;
    int lines = 0;
    auto flush = [&]() {
        canvas.drawString(line, x, y + lines * lineH);
        lines++;
        line = "";
    };
    for (const char* p = text;; p++) {
        if (*p == ' ' || *p == '\n' || *p == 0) {
            String candidate = line.length() ? line + " " + word : word;
            if (line.length() && canvas.textWidth(candidate) > w) {
                flush();
                line = word;
            } else {
                line = candidate;
            }
            word = "";
            if (*p == '\n') flush();
            if (*p == 0) break;
        } else {
            word += *p;
        }
    }
    if (line.length()) flush();
    return lines * lineH;
}

void progressBar(int x, int y, int w, int h, float t, uint16_t color) {
    t = constrain(t, 0.0f, 1.0f);
    canvas.fillSmoothRoundRect(x, y, w, h, h / 2, col::surface2);
    int fw = max((float)h, w * t);
    if (t > 0) canvas.fillSmoothRoundRect(x, y, fw, h, h / 2, color);
}

void scrollbar(int x, int y, int h, float offset, float visible, float total) {
    if (total <= visible) return;
    int thumb = max(8.0f, h * visible / total);
    int ty = y + (h - thumb) * (offset / (total - visible));
    canvas.fillRoundRect(x, y, 3, h, 1, col::surface2);
    canvas.fillRoundRect(x, ty, 3, thumb, 1, col::muted);
}

void darken() {
    auto* px = (uint16_t*)canvas.getBuffer();
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t c = __builtin_bswap16(px[i]);  // sprite stores big-endian RGB565
        c = (c >> 2) & 0x39E7;                  // quarter brightness per channel
        px[i] = __builtin_bswap16(c);
    }
}

void toast(const char* msg, uint16_t color) {
    strlcpy(toastState.msg, msg, sizeof(toastState.msg));
    toastState.color = color;
    toastState.until = millis() + 1800;
}

bool toastVisible() { return millis() < toastState.until; }

void drawToast() {
    if (!toastVisible()) return;
    canvas.setFont(font::body());
    int w = canvas.textWidth(toastState.msg) + 20, h = 19;
    int x = (SCREEN_W - w) / 2, y = FOOTER_Y - h - 5;
    canvas.fillSmoothRoundRect(x, y, w, h, 9, col::surface2);
    canvas.drawRoundRect(x, y, w, h, 9, toastState.color);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(toastState.color);
    canvas.drawString(toastState.msg, SCREEN_W / 2, y + h / 2 + 1);
}

}  // namespace ui
