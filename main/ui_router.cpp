#include "ui_router.h"

namespace ui {

namespace {

constexpr uint32_t kSavedHoldMs = 3500;
constexpr uint16_t kAmbientOutMs = 500;
constexpr uint16_t kAmbientInMs = 600;

struct Offset {
    int8_t x;
    int8_t y;
};
constexpr Offset kShift[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int kShiftCount = sizeof(kShift) / sizeof(kShift[0]);

float progress(uint32_t now, uint32_t start, uint32_t duration)
{
    if (duration == 0) return 1;
    uint32_t elapsed = now - start;
    return elapsed >= duration ? 1.0f : (float)elapsed / duration;
}

uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
    return (uint8_t)(a + (b - a) * t + 0.5f);
}

} // namespace

void Router::begin(const RouterConfig &cfg, bool start_in_setup, uint32_t now)
{
    cfg_ = cfg;
    now_ = now;
    last_input_ = now;
    page_ = start_in_setup ? Page::Setup : Page::Card;
    setup_activity_ = now;
    stage_ = SetupStage::Waiting;
    brightness_ = base_brightness(page_);
}

Haptic Router::take_haptic()
{
    Haptic h = haptic_;
    haptic_ = Haptic::None;
    return h;
}

bool Router::take_scan_event()
{
    bool e = scan_event_;
    scan_event_ = false;
    return e;
}

uint8_t Router::base_brightness(Page page) const
{
    switch (page) {
    case Page::Scan: return kScanBrightness;
    case Page::Ambient: return cfg_.ambient_brightness;
    default: return cfg_.brightness;
    }
}

void Router::finish_anim(uint32_t now)
{
    if (anim_ == Anim::Dip && !dip_switched_) page_ = dip_target_;
    anim_ = Anim::None;
    update_brightness(now);
}

void Router::slide_to(Page target, int dir, uint32_t now)
{
    from_ = page_;
    page_ = target;
    slide_dir_ = dir;
    anim_ = Anim::Slide;
    anim_start_ = now;
    haptic_ = Haptic::Tick;
}

void Router::dip_to(Page target, uint32_t now, uint16_t out_ms, uint16_t in_ms)
{
    anim_ = Anim::Dip;
    anim_start_ = now;
    dip_target_ = target;
    dip_switched_ = false;
    dip_out_ms_ = out_ms;
    dip_in_ms_ = in_ms;
    anim_from_brightness_ = brightness_;
}

void Router::open_scan(uint32_t now)
{
    if (page_ == Page::Card || page_ == Page::About) return_page_ = page_;
    else return_page_ = Page::Card;
    page_ = Page::Scan;
    anim_ = Anim::ScanIn;
    anim_start_ = now;
    anim_from_brightness_ = brightness_;
    scan_start_ = now;
    qr_index_ = 0;
    haptic_ = Haptic::Tick;
    scan_event_ = true;
}

void Router::close_scan(uint32_t now)
{
    dip_to(return_page_, now, kDipOutMs, kDipInMs);
}

void Router::next_qr(int step, uint32_t now)
{
    if (cfg_.qr_count <= 1) return;
    qr_index_ = (qr_index_ + step + cfg_.qr_count) % cfg_.qr_count;
    scan_start_ = now;
    haptic_ = Haptic::Tick;
}

void Router::enter_setup(uint32_t now)
{
    stage_ = SetupStage::Waiting;
    setup_activity_ = now;
    haptic_ = Haptic::Double;
    dip_to(Page::Setup, now, kDipOutMs, kDipInMs);
}

void Router::set_setup_stage(SetupStage stage, uint32_t now)
{
    if (stage == stage_) return;
    stage_ = stage;
    setup_activity_ = now;
    if (stage == SetupStage::Saved) {
        saved_at_ = now;
        haptic_ = Haptic::Double;
    }
}

void Router::input(Input in, uint32_t now)
{
    const bool touch = in == Input::Tap || in == Input::SwipeLeft || in == Input::SwipeRight;
    if (touch && !cfg_.touch_enabled) return;
    if (anim_ != Anim::None) finish_anim(now);
    last_input_ = now;

    if (in == Input::KeyBLong) {
        if (page_ == Page::Setup) {
            haptic_ = Haptic::Double;
            dip_to(Page::Card, now, kDipOutMs, kDipInMs);
        } else {
            enter_setup(now);
        }
        return;
    }

    switch (page_) {
    case Page::Card:
    case Page::About: {
        const bool on_card = page_ == Page::Card;
        if (in == Input::KeyA) open_scan(now);
        else if (cfg_.about_enabled && in == Input::KeyB) slide_to(on_card ? Page::About : Page::Card, on_card ? 1 : -1, now);
        else if (cfg_.about_enabled && in == Input::SwipeLeft && on_card) slide_to(Page::About, 1, now);
        else if (cfg_.about_enabled && in == Input::SwipeRight && !on_card) slide_to(Page::Card, -1, now);
        break;
    }
    case Page::Scan:
        if (in == Input::KeyA) close_scan(now);
        else if (in == Input::KeyB || in == Input::Tap || in == Input::SwipeLeft) {
            if (cfg_.qr_count > 1) next_qr(1, now);
            else if (in == Input::KeyB) close_scan(now);
            else scan_start_ = now;
        } else if (in == Input::SwipeRight) next_qr(-1, now);
        break;
    case Page::Ambient:
        if (in == Input::KeyA) open_scan(now);
        else dip_to(Page::Card, now, 120, kAmbientInMs);
        break;
    case Page::Setup:
        setup_activity_ = now;
        break;
    }
}

void Router::update_brightness(uint32_t now)
{
    switch (anim_) {
    case Anim::None:
    case Anim::Slide:
        brightness_ = base_brightness(page_);
        break;
    case Anim::ScanIn:
        brightness_ = lerp_u8(anim_from_brightness_, kScanBrightness, progress(now, anim_start_, kScanInMs));
        break;
    case Anim::Dip: {
        uint32_t elapsed = now - anim_start_;
        if (elapsed < dip_out_ms_) {
            brightness_ = lerp_u8(anim_from_brightness_, 0, progress(now, anim_start_, dip_out_ms_));
        } else {
            float t = progress(now, anim_start_ + dip_out_ms_, dip_in_ms_);
            brightness_ = lerp_u8(0, base_brightness(dip_target_), ease_out_cubic(t));
        }
        break;
    }
    }
}

uint32_t Router::visual_key(uint32_t now) const
{
    uint32_t key = (uint32_t)page_;
    key = key * 31 + (uint32_t)qr_index_;
    key = key * 31 + (uint32_t)stage_;
    Frame f{};
    fill(f);
    key = key * 31 + (uint32_t)(f.shift_x + 16);
    key = key * 31 + (uint32_t)(f.shift_y + 16);
    if (page_ == Page::Scan) key = key * 61 + (uint32_t)(f.countdown * kDotCount + 0.999f);
    if (page_ == Page::Setup) key = key * 61 + (uint32_t)(f.setup.remaining * kDotCount + 0.999f);
    (void)now;
    return key;
}

bool Router::tick(uint32_t now)
{
    now_ = now;
    bool dirty = false;

    if (anim_ == Anim::Dip && !dip_switched_ && now - anim_start_ >= dip_out_ms_) {
        page_ = dip_target_;
        dip_switched_ = true;
        if (page_ == Page::Scan) scan_start_ = now;
        dirty = true;
    }
    if (anim_ != Anim::None) {
        uint32_t duration = anim_ == Anim::Slide ? kSlideMs
                            : anim_ == Anim::ScanIn ? kScanInMs
                                                    : (uint32_t)dip_out_ms_ + dip_in_ms_;
        // A dip only changes brightness, so pixels change once at the switch.
        if (anim_ != Anim::Dip) dirty = true;
        if (now - anim_start_ >= duration) anim_ = Anim::None;
    }

    if (anim_ == Anim::None) {
        switch (page_) {
        case Page::Card:
        case Page::About:
            if (cfg_.ambient_after_ms && now - last_input_ >= cfg_.ambient_after_ms)
                dip_to(Page::Ambient, now, kAmbientOutMs, kAmbientInMs);
            break;
        case Page::Scan:
            if (now - scan_start_ >= cfg_.scan_timeout_ms) {
                last_input_ = now;
                close_scan(now);
            }
            break;
        case Page::Setup:
            if (stage_ == SetupStage::Saved && now - saved_at_ >= kSavedHoldMs) {
                last_input_ = now;
                dip_to(Page::Card, now, kDipOutMs, kDipInMs);
            } else if (now - setup_activity_ >= cfg_.setup_timeout_ms) {
                last_input_ = now;
                dip_to(Page::Card, now, kDipOutMs, kDipInMs);
            }
            break;
        case Page::Ambient:
            break;
        }
    }

    update_brightness(now);
    uint32_t key = visual_key(now);
    if (key != last_key_) {
        last_key_ = key;
        dirty = true;
    }
    return dirty || anim_ == Anim::Slide || anim_ == Anim::ScanIn;
}

void Router::fill(Frame &f) const
{
    f.page = page_;
    f.sliding = anim_ == Anim::Slide;
    f.from = from_;
    f.slide = anim_ == Anim::Slide ? progress(now_, anim_start_, kSlideMs) : 1.0f;
    f.slide_dir = slide_dir_;
    f.scan_enter = anim_ == Anim::ScanIn ? progress(now_, anim_start_, kScanInMs) : 1.0f;
    f.qr_index = qr_index_;
    f.countdown = 1.0f;
    if (page_ == Page::Scan && cfg_.scan_timeout_ms) {
        uint32_t elapsed = now_ - scan_start_;
        f.countdown = elapsed >= cfg_.scan_timeout_ms ? 0 : 1.0f - (float)elapsed / cfg_.scan_timeout_ms;
    }
    // Shift every minute to spread wear on static AMOLED pixels.
    const Offset &o = kShift[(now_ / 60000) % kShiftCount];
    const int scale = page_ == Page::Ambient ? 8 : (page_ == Page::Card || page_ == Page::About) ? 3 : 0;
    f.shift_x = o.x * scale;
    f.shift_y = o.y * scale;
    f.setup.stage = stage_;
    f.setup.remaining = 1.0f;
    if (cfg_.setup_timeout_ms) {
        uint32_t idle = now_ - setup_activity_;
        f.setup.remaining = idle >= cfg_.setup_timeout_ms ? 0 : 1.0f - (float)idle / cfg_.setup_timeout_ms;
    }
}

} // namespace ui
