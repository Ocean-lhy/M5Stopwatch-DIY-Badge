#pragma once

#include <stdint.h>

#include "ui_pages.h"

namespace ui {

enum class Input : uint8_t { KeyA, KeyB, KeyBLong, Tap, SwipeLeft, SwipeRight };
enum class Haptic : uint8_t { None, Tick, Double };

struct RouterConfig {
    uint32_t ambient_after_ms; // 0 = never enter ambient
    uint32_t scan_timeout_ms;
    uint32_t setup_timeout_ms;
    uint8_t brightness;
    uint8_t ambient_brightness;
    bool about_enabled;
    bool touch_enabled;
    int qr_count;
};

// Owns page state, transitions, timeouts and the brightness envelope. It has no
// hardware dependencies so the desktop simulator drives exactly the same logic.
class Router {
public:
    void begin(const RouterConfig &cfg, bool start_in_setup, uint32_t now);
    void configure(const RouterConfig &cfg) { cfg_ = cfg; }

    void input(Input in, uint32_t now);
    void portal_activity(uint32_t now) { setup_activity_ = now; }
    void set_setup_stage(SetupStage stage, uint32_t now);
    void enter_setup(uint32_t now);

    // Advances timeouts and animations. Returns true when the screen must be redrawn.
    bool tick(uint32_t now);
    void fill(Frame &frame) const;

    Page page() const { return page_; }
    bool animating() const { return anim_ != Anim::None; }
    uint8_t brightness() const { return brightness_; }
    bool want_portal() const { return page_ == Page::Setup || (anim_ == Anim::Dip && dip_target_ == Page::Setup); }
    Haptic take_haptic();
    bool take_scan_event();

private:
    enum class Anim : uint8_t { None, Slide, ScanIn, Dip };

    void finish_anim(uint32_t now);
    void slide_to(Page target, int dir, uint32_t now);
    void dip_to(Page target, uint32_t now, uint16_t out_ms, uint16_t in_ms);
    void open_scan(uint32_t now);
    void close_scan(uint32_t now);
    void next_qr(int step, uint32_t now);
    uint8_t base_brightness(Page page) const;
    void update_brightness(uint32_t now);
    uint32_t visual_key(uint32_t now) const;

    RouterConfig cfg_{};
    Page page_ = Page::Card;
    Page return_page_ = Page::Card;
    Anim anim_ = Anim::None;
    uint32_t anim_start_ = 0;
    Page from_ = Page::Card;
    Page dip_target_ = Page::Card;
    bool dip_switched_ = false;
    uint16_t dip_out_ms_ = 0;
    uint16_t dip_in_ms_ = 0;
    int slide_dir_ = 1;
    uint8_t anim_from_brightness_ = 0;
    uint8_t brightness_ = 0;

    uint32_t now_ = 0;
    uint32_t last_input_ = 0;
    uint32_t scan_start_ = 0;
    uint32_t setup_activity_ = 0;
    uint32_t saved_at_ = 0;
    SetupStage stage_ = SetupStage::Waiting;
    int qr_index_ = 0;
    uint32_t last_key_ = 0xFFFFFFFF;
    Haptic haptic_ = Haptic::None;
    bool scan_event_ = false;
};

} // namespace ui
