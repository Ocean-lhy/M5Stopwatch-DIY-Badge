#include "ui_pages.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lgfx/utility/lgfx_qrcode.h"

namespace ui {

namespace {

constexpr float kPi = 3.14159265f;
constexpr uint16_t kMuted = rgb565(138, 155, 176);

struct Dot {
    int16_t x;
    int16_t y;
};

const Dot *dot_ring()
{
    static Dot dots[kDotCount];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < kDotCount; ++i) {
            // Start at 12 o'clock and run clockwise.
            float a = -kPi / 2 + 2 * kPi * i / kDotCount;
            dots[i].x = (int16_t)lroundf(kCenter + kDotRingRadius * cosf(a));
            dots[i].y = (int16_t)lroundf(kCenter + kDotRingRadius * sinf(a));
        }
        ready = true;
    }
    return dots;
}

// Lit dots drain back towards 12 o'clock as the remaining time shrinks.
void draw_dot_ring(lgfx::LGFX_Sprite &dst, float remaining, uint16_t lit, uint16_t dim)
{
    const Dot *dots = dot_ring();
    if (remaining < 0) remaining = 0;
    if (remaining > 1) remaining = 1;
    int lit_count = (int)ceilf(remaining * kDotCount);
    for (int i = 0; i < kDotCount; ++i) {
        if (i < lit_count) dst.fillSmoothCircle(dots[i].x, dots[i].y, 3, lit);
        else dst.fillSmoothCircle(dots[i].x, dots[i].y, 2, dim);
    }
}

void draw_centered(lgfx::LGFX_Sprite &dst, const char *text, int y, const lgfx::IFont *font, uint16_t color)
{
    dst.setFont(font);
    dst.setTextDatum(textdatum_t::middle_center);
    dst.setTextColor(color);
    dst.drawString(text, kCenter, y);
}

// "label value" centered as one line; CJK labels need the efont, Latin ones use DejaVu.
void draw_pair(lgfx::LGFX_Sprite &dst, const char *label, const lgfx::IFont *label_font, const char *value, int y,
               uint16_t label_color, uint16_t value_color)
{
    dst.setFont(label_font);
    const int lw = dst.textWidth(label);
    dst.setFont(&fonts::DejaVu24);
    const int vw = dst.textWidth(value);
    const int gap = 10;
    int x = kCenter - (lw + gap + vw) / 2;
    dst.setTextDatum(textdatum_t::middle_left);
    dst.setFont(label_font);
    dst.setTextColor(label_color);
    dst.drawString(label, x, y);
    dst.setFont(&fonts::DejaVu24);
    dst.setTextColor(value_color);
    dst.drawString(value, x + lw + gap, y);
}

void draw_battery_glyph(lgfx::LGFX_Sprite &dst, int x, int y, int pct, bool charging, uint16_t frame,
                        uint16_t fill)
{
    const int w = 24, h = 12;
    dst.drawRoundRect(x, y, w, h, 3, frame);
    dst.fillRect(x + w, y + 3, 2, h - 6, frame);
    int inner = (w - 4) * (pct < 0 ? 0 : pct > 100 ? 100 : pct) / 100;
    if (inner > 0) dst.fillRoundRect(x + 2, y + 2, inner, h - 4, 1, fill);
    if (charging) {
        const int cx = x + w / 2, cy = y + h / 2;
        dst.fillTriangle(cx + 1, cy - 7, cx - 4, cy + 1, cx, cy + 1, frame);
        dst.fillTriangle(cx - 1, cy + 7, cx + 4, cy - 1, cx, cy - 1, frame);
    }
}

uint16_t battery_color(int pct, bool charging, uint16_t normal)
{
    if (charging) return kOk;
    if (pct >= 0 && pct <= 10) return kLow;
    if (pct >= 0 && pct <= 20) return kWarn;
    return normal;
}

void format_runtime(char *out, size_t len, float hours)
{
    if (hours >= 99.5f) snprintf(out, len, "99h+");
    else snprintf(out, len, "%.1fh", hours);
}

// Battery glyph plus optional runtime estimate, laid out from `x`; returns the width used.
int draw_battery_block(lgfx::LGFX_Sprite &dst, int x, int cy, const Status &s, uint16_t color, bool measure_only)
{
    if (s.battery < 0) return 0;
    char runtime[12] = {};
    if (!s.charging && s.runtime_h >= 0) format_runtime(runtime, sizeof(runtime), s.runtime_h);
    dst.setFont(&fonts::DejaVu18);
    const int rw = runtime[0] ? dst.textWidth(runtime) : 0;
    const int width = 26 + (rw ? 8 + rw : 0);
    if (measure_only) return width;
    draw_battery_glyph(dst, x, cy - 6, s.battery, s.charging, color, battery_color(s.battery, s.charging, color));
    if (rw) {
        dst.setTextDatum(textdatum_t::middle_left);
        dst.setTextColor(color);
        dst.drawString(runtime, x + 34, cy);
    }
    return width;
}

void draw_check(lgfx::LGFX_Sprite &dst, int cx, int cy, int r, uint16_t color)
{
    dst.fillSmoothCircle(cx, cy, r, color);
    dst.drawWideLine(cx - r * 4 / 10, cy + r / 20, cx - r / 10, cy + r * 3 / 10, r / 11.0f, kScanBg);
    dst.drawWideLine(cx - r / 10, cy + r * 3 / 10, cx + r * 45 / 100, cy - r * 3 / 10, r / 11.0f, kScanBg);
}

} // namespace

float ease_out_cubic(float t)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float u = 1 - t;
    return 1 - u * u * u;
}

float frame_emission(const lgfx::LGFX_Sprite &frame)
{
    // Gamma-2.2 decode of 5/6-bit channels; AMOLED current follows emitted light.
    static float lut5[32], lut6[64];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 32; ++i) lut5[i] = powf(i / 31.0f, 2.2f);
        for (int i = 0; i < 64; ++i) lut6[i] = powf(i / 63.0f, 2.2f);
        ready = true;
    }
    const auto *buf = static_cast<const uint16_t *>(const_cast<lgfx::LGFX_Sprite &>(frame).getBuffer());
    if (!buf) return 0;
    const int w = frame.width(), h = frame.height();
    constexpr int kStep = 6;
    const int r2 = kRadius * kRadius;
    float sum = 0;
    int count = 0;
    for (int y = kStep / 2; y < h; y += kStep) {
        const int dy = y - kCenter;
        for (int x = kStep / 2; x < w; x += kStep) {
            const int dx = x - kCenter;
            if (dx * dx + dy * dy > r2) continue;
            uint16_t p = buf[y * w + x];
            p = (uint16_t)((p >> 8) | (p << 8)); // sprites store RGB565 big-endian
            sum += (lut5[p >> 11] + lut6[(p >> 5) & 0x3F] + lut5[p & 0x1F]) * (1.0f / 3);
            ++count;
        }
    }
    return count ? sum / count : 0;
}

int Renderer::take_qr_mismatches()
{
    int m = qr_mismatches_;
    qr_mismatches_ = -1;
    return m;
}

bool Renderer::encode_qr(const char *text)
{
    if (!text || !text[0]) return false;
    if (qr_size_ > 0 && strncmp(qr_key_, text, sizeof(qr_key_)) == 0) return true;
    qr_size_ = 0;
    // Medium ECC tolerates glare on the glass; versions grow until the text fits.
    for (uint8_t version = 1; version <= 13; ++version) {
        QRCode qr;
        uint8_t buf[600];
        if (lgfx_qrcode_getBufferSize(version) > sizeof(buf)) break;
        if (lgfx_qrcode_initText(&qr, buf, version, ECC_MEDIUM, text) != 0) continue;
        if (qr.size > kMaxQrSize) break;
        memset(qr_bits_, 0, sizeof(qr_bits_));
        qr_size_ = qr.size;
        for (int y = 0; y < qr.size; ++y)
            for (int x = 0; x < qr.size; ++x)
                if (lgfx_qrcode_getModule(&qr, x, y)) qr_bits_[(y * qr_size_ + x) >> 3] |= 1u << ((y * qr_size_ + x) & 7);
        snprintf(qr_key_, sizeof(qr_key_), "%s", text);
        qr_verify_ = true;
        return true;
    }
    return false;
}

void Renderer::draw_qr(lgfx::LGFX_Sprite &dst, int x, int y, int side)
{
    if (qr_size_ <= 0) return;
    // Integer module size with at least a 4-module quiet zone inside the plate.
    const int module = side / (qr_size_ + 8) < 2 ? 2 : side / (qr_size_ + 8);
    const int ox = x + (side - qr_size_ * module) / 2;
    const int oy = y + (side - qr_size_ * module) / 2;
    for (int row = 0; row < qr_size_; ++row) {
        for (int col = 0; col < qr_size_; ++col) {
            if (qr_module(col, row)) dst.fillRect(ox + col * module, oy + row * module, module, module, kScanBg);
        }
    }
    if (qr_verify_) {
        qr_verify_ = false;
        int bad = 0;
        const int half = module / 2;
        for (int row = 0; row < qr_size_; ++row) {
            for (int col = 0; col < qr_size_; ++col) {
                const bool dark = dst.readPixel(ox + col * module + half, oy + row * module + half) == 0;
                if (dark != qr_module(col, row)) ++bad;
            }
        }
        qr_mismatches_ = bad;
    }
}

void Renderer::draw_layer_page(lgfx::LGFX_Sprite &dst, Page page, int dx, int dy, const Content &c)
{
    lgfx::LGFX_Sprite *layer = page == Page::About ? c.about : c.card;
    if (layer && layer->getBuffer()) {
        layer->pushSprite(&dst, dx, dy);
        return;
    }
    // Native fallback until the web editor has published a rendered layer.
    const Theme &t = c.theme;
    dst.fillRect(dx, dy, kSize, kSize, t.bg);
    dst.setTextDatum(textdatum_t::middle_center);
    if (page == Page::About) {
        dst.setFont(&fonts::efontCN_16);
        dst.setTextColor(t.muted);
        dst.drawString("未设置介绍页", kCenter + dx, kCenter + dy);
        return;
    }
    dst.setFont(&fonts::efontCN_24_b);
    dst.setTextColor(t.ink);
    dst.drawString(c.name && c.name[0] ? c.name : "M5 Badge", kCenter + dx, 214 + dy);
    dst.setFont(&fonts::efontCN_16);
    dst.setTextColor(t.muted);
    if (c.title && c.title[0]) dst.drawString(c.title, kCenter + dx, 254 + dy);
}

void Renderer::draw_status(lgfx::LGFX_Sprite &dst, const Content &c, const Status &s)
{
    char time_text[8] = {};
    if (s.time_valid) snprintf(time_text, sizeof(time_text), "%02d:%02d", s.hour, s.minute);
    dst.setFont(&fonts::DejaVu18);
    const int time_w = time_text[0] ? dst.textWidth(time_text) : 0;
    const int batt_w = draw_battery_block(dst, 0, kStatusY, s, c.theme.muted, true);
    const int gap = time_w && batt_w ? 12 : 0;
    int x = kCenter - (time_w + gap + batt_w) / 2;
    if (time_w) {
        dst.setFont(&fonts::DejaVu18);
        dst.setTextDatum(textdatum_t::middle_left);
        dst.setTextColor(c.theme.muted);
        dst.drawString(time_text, x, kStatusY);
        x += time_w + gap;
    }
    draw_battery_block(dst, x, kStatusY, s, c.theme.muted, false);
}

void Renderer::draw_scan(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c)
{
    dst.fillSprite(kScanBg);
    const float t = ease_out_cubic(f.scan_enter);
    const int plate_x = kCenter - kPlateSize / 2;
    const int plate_y = kPlateY + (int)lroundf((1 - t) * 56);
    const int index = c.qr_count > 0 ? (f.qr_index % c.qr_count) : 0;
    const QrSlot *slot = c.qr_count > 0 ? &c.qr[index] : nullptr;

    draw_dot_ring(dst, f.countdown, c.theme.accent, rgb565(28, 40, 54));
    dst.fillSmoothRoundRect(plate_x, plate_y, kPlateSize, kPlateSize, kPlateRadius, kPlate);
    if (slot && encode_qr(slot->text)) {
        draw_qr(dst, plate_x, plate_y, kPlateSize);
    } else {
        dst.setFont(&fonts::efontCN_16);
        dst.setTextDatum(textdatum_t::middle_center);
        dst.setTextColor(rgb565(90, 90, 90));
        dst.drawString("未设置二维码", kCenter, plate_y + kPlateSize / 2);
    }

    const int label_y = plate_y + kPlateSize + 34;
    if (slot && slot->label && slot->label[0]) draw_centered(dst, slot->label, label_y, &fonts::efontCN_24_b, kPlate);

    if (c.qr_count > 1) {
        const int pitch = 18;
        int x = kCenter - (c.qr_count - 1) * pitch / 2;
        for (int i = 0; i < c.qr_count; ++i, x += pitch) {
            if (i == index) dst.fillSmoothRoundRect(x - 8, label_y + 30, 16, 6, 3, c.theme.accent);
            else dst.fillSmoothCircle(x, label_y + 33, 3, rgb565(70, 80, 92));
        }
    }
}

void Renderer::draw_ambient(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c, const Status &s)
{
    dst.fillSprite(kScanBg);
    const int sx = f.shift_x, sy = f.shift_y;
    if (s.time_valid) {
        char text[8];
        snprintf(text, sizeof(text), "%02d:%02d", s.hour, s.minute);
        dst.setFont(&fonts::DejaVu72);
        dst.setTextDatum(textdatum_t::middle_center);
        dst.setTextColor(kAmbientInk);
        dst.drawString(text, kCenter + sx, 186 + sy);
    }
    if (c.name_strip && c.name_strip->getBuffer()) {
        c.name_strip->pushSprite(&dst, kCenter - c.name_strip->width() / 2 + sx, 236 + sy);
    } else if (c.name && c.name[0]) {
        draw_centered(dst, c.name, 266 + sy, &fonts::efontCN_24, kAmbientDim);
    }
    const int w = draw_battery_block(dst, 0, 358 + sy, s, kAmbientDim, true);
    draw_battery_block(dst, kCenter - w / 2 + sx, 358 + sy, s, kAmbientDim, false);
}

void Renderer::draw_setup(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c)
{
    dst.fillSprite(kScanBg);
    const Setup &s = f.setup;
    draw_dot_ring(dst, s.remaining, rgb565(60, 92, 128), rgb565(24, 32, 42));

    switch (s.stage) {
    case SetupStage::Waiting: {
        const int x = kCenter - kSetupPlate / 2;
        dst.fillSmoothRoundRect(x, kSetupPlateY, kSetupPlate, kSetupPlate, 24, kPlate);
        if (encode_qr(s.wifi_qr)) draw_qr(dst, x, kSetupPlateY, kSetupPlate);
        const int below = kSetupPlateY + kSetupPlate;
        draw_pair(dst, "Wi-Fi", &fonts::DejaVu18, s.ssid ? s.ssid : "", below + 36, kMuted, kPlate);
        draw_pair(dst, "密码", &fonts::efontCN_16, s.password ? s.password : "", below + 70, kMuted, kPlate);
        break;
    }
    case SetupStage::Connected:
        draw_centered(dst, "已连接", 196, &fonts::efontCN_24_b, kPlate);
        draw_centered(dst, s.url ? s.url : "192.168.4.1", 246, &fonts::DejaVu24, c.theme.accent);
        break;
    case SetupStage::Saved:
        draw_check(dst, kCenter, 206, 60, kOk);
        draw_centered(dst, "已保存", 300, &fonts::efontCN_24_b, kPlate);
        break;
    }
}

void Renderer::draw(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c, const Status &s)
{
    switch (f.page) {
    case Page::Card:
    case Page::About:
        if (f.sliding) {
            const int off = (int)lroundf(ease_out_cubic(f.slide) * kSize);
            dst.fillSprite(c.theme.bg);
            draw_layer_page(dst, f.from, -f.slide_dir * off, 0, c);
            draw_layer_page(dst, f.page, f.slide_dir * (kSize - off), 0, c);
        } else {
            if (f.shift_x || f.shift_y) dst.fillSprite(c.theme.bg);
            draw_layer_page(dst, f.page, f.shift_x, f.shift_y, c);
        }
        draw_status(dst, c, s);
        break;
    case Page::Scan:
        draw_scan(dst, f, c);
        break;
    case Page::Ambient:
        draw_ambient(dst, f, c, s);
        break;
    case Page::Setup:
        draw_setup(dst, f, c);
        break;
    }
}

} // namespace ui
