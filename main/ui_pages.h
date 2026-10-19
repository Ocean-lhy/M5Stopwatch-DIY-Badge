#pragma once

#include <stdint.h>

#include <M5GFX.h>

#include "ui_theme.h"

namespace ui {

enum class Page : uint8_t { Card, About, Scan, Ambient, Setup };

constexpr int kMaxQr = 3;

struct QrSlot {
    const char *label;
    const char *text;
};

// Everything the pages need, owned by the caller. Layers are RGB565 sprites
// rendered by the web editor (card/about 466x466, name strip smaller); any of
// them may be null, in which case a native fallback is drawn.
struct Content {
    Theme theme;
    lgfx::LGFX_Sprite *card;
    lgfx::LGFX_Sprite *about;
    lgfx::LGFX_Sprite *name_strip;
    const char *name;
    const char *title;
    QrSlot qr[kMaxQr];
    int qr_count;
};

struct Status {
    int battery;     // -1 = unknown
    float runtime_h; // estimated hours left, < 0 = unknown
    bool charging;
    bool time_valid;
    int hour;
    int minute;
};

enum class SetupStage : uint8_t { Waiting, Connected, Saved };

struct Setup {
    const char *ssid;
    const char *password;
    const char *wifi_qr;
    const char *url;
    SetupStage stage;
    float remaining; // 0..1 of the session timeout
};

struct Frame {
    Page page;
    bool sliding;     // card <-> about slide in progress
    Page from;
    float slide;      // 0..1
    int slide_dir;    // +1: new page enters from the right
    float scan_enter; // 0..1
    float countdown;  // 0..1 remaining on the scan page
    int qr_index;
    int shift_x;      // burn-in protection
    int shift_y;
    Setup setup;
};

class Renderer {
public:
    void draw(lgfx::LGFX_Sprite &dst, const Frame &frame, const Content &content, const Status &status);
    // Encodes and rasterizes the first code so opening the scan page only blits it.
    void prepare(const Content &content);
    void invalidate()
    {
        qr_key_[0] = 0;
        qr_raster_ok_ = false;
    }
    // Modules that read back wrong the first time a new code was drawn (-1 = not checked yet).
    int take_qr_mismatches();

private:
    void draw_layer_page(lgfx::LGFX_Sprite &dst, Page page, int dx, int dy, const Content &c);
    void draw_status(lgfx::LGFX_Sprite &dst, const Content &c, const Status &s);
    void draw_scan(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c);
    void draw_ambient(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c, const Status &s);
    void draw_setup(lgfx::LGFX_Sprite &dst, const Frame &f, const Content &c);
    bool encode_qr(const char *text);
    // Builds a sprite of the current code at `module` pixels per cell. Reused across frames.
    bool rasterize_qr(int module);
    // Draws the cached code centered in a white square of `side` pixels at (x, y).
    void draw_qr(lgfx::LGFX_Sprite &dst, int x, int y, int side);
    bool qr_module(int x, int y) const { return qr_bits_[(y * qr_size_ + x) >> 3] & (1u << ((y * qr_size_ + x) & 7)); }

    static constexpr int kMaxQrSize = 69; // version 13
    char qr_key_[300] = {};
    uint8_t qr_bits_[(kMaxQrSize * kMaxQrSize + 7) / 8] = {};
    int qr_size_ = 0;
    bool qr_verify_ = false;
    int qr_mismatches_ = -1;
    lgfx::LGFX_Sprite qr_sprite_;
    int qr_raster_module_ = 0;
    int qr_raster_size_ = 0;
    uint16_t qr_dark_ = 0;
    bool qr_raster_ok_ = false;
};

float ease_out_cubic(float t);
// Average linear light output of the visible round area (0 = black, 1 = full white).
float frame_emission(const lgfx::LGFX_Sprite &frame);

} // namespace ui
