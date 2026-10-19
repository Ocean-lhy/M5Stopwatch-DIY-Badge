#include <string.h>
#include <time.h>

#include "badge_config.h"
#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "haptics.h"
#include "input.h"
#include "layers.h"
#include "nvs_flash.h"
#include "power.h"
#include "rtc_time.h"
#include "ui_pages.h"
#include "ui_router.h"
#include "wifi_portal.h"

static const char *TAG = "badge_main";
static constexpr uint32_t kSetupTimeoutMs = 5 * 60 * 1000;

static BadgeConfig s_config; // shared with the HTTP task, guarded by portal_lock()
static BadgeConfig s_view;   // main-task copy used for drawing
static Layers s_layers;

static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

static ui::RouterConfig router_config(const BadgeConfig &c)
{
    ui::RouterConfig r{};
    r.ambient_after_ms = (uint32_t)c.ambient_after_s * 1000;
    r.scan_timeout_ms = (uint32_t)c.scan_timeout_s * 1000;
    r.setup_timeout_ms = kSetupTimeoutMs;
    r.brightness = c.brightness;
    r.ambient_brightness = c.ambient_brightness;
    r.about_enabled = c.about_enabled;
    r.touch_enabled = c.touch_enabled;
    r.qr_count = c.qr_count;
    return r;
}

static void build_content(const BadgeConfig &c, ui::Content &out)
{
    out = {};
    out.theme = {c.bg, c.ink, c.muted, c.accent, c.muted};
    out.card = s_layers.card.getBuffer() ? &s_layers.card : nullptr;
    out.about = s_layers.about.getBuffer() ? &s_layers.about : nullptr;
    out.name_strip = s_layers.name.getBuffer() ? &s_layers.name : nullptr;
    out.name = c.name;
    out.title = c.title;
    out.qr_count = c.qr_count > ui::kMaxQr ? ui::kMaxQr : c.qr_count;
    for (int i = 0; i < out.qr_count; ++i) out.qr[i] = {c.qr[i].label, c.qr[i].text};
}

static void read_status(ui::Status &s)
{
    PowerState p = power_state();
    s.battery = p.percent;
    s.charging = p.charging;
    // Rounded to what the status row shows, so redraws happen only on visible changes.
    s.runtime_h = p.runtime_h < 0 ? -1 : (int)(p.runtime_h * 10 + 0.5f) / 10.0f;
    struct tm local = {};
    s.time_valid = rtc_time_local(&local);
    s.hour = local.tm_hour;
    s.minute = local.tm_min;
}

extern "C" void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    const bool published = badge_config_load(&s_config);
    board_init();
    input_init(board_i2c());
    rtc_time_init(board_i2c(), s_config.tz_offset_min);
    display_init();
    portal_init(&s_config);

    s_view = s_config;
    s_layers.reload(s_view.layers);
    power_update(now_ms());

    ui::Renderer renderer;
    ui::Router router;
    ui::Content content;
    ui::Status status{};
    build_content(s_view, content);
    read_status(status);
    router.begin(router_config(s_view), !published, now_ms());
    ESP_LOGI(TAG, "badge ready, published=%d layers=0x%02x", published, s_view.layers);

    uint32_t seen_activity = 0;
    uint32_t last_brightness_ms = 0;
    uint32_t last_load_ms = 0;
    float frame_level = 0;
    bool force = true;
    ui::Input events[8];

    for (;;) {
        const uint32_t now = now_ms();

        int count = input_poll(now, events, 8);
        for (int i = 0; i < count; ++i) router.input(events[i], now);

        if (uint32_t changes = portal_take_changes()) {
            portal_lock();
            s_view = s_config;
            portal_unlock();
            const uint8_t layers = (changes >> PORTAL_LAYER_SHIFT) & 0xFF;
            if (layers) s_layers.reload(layers);
            if (changes & PORTAL_TIME) rtc_time_set_tz(s_view.tz_offset_min);
            router.configure(router_config(s_view));
            build_content(s_view, content);
            renderer.invalidate();
            if (changes & PORTAL_PUBLISHED) router.set_setup_stage(ui::SetupStage::Saved, now);
            force = true;
        }

        // The SoftAP only exists while the setup page is shown.
        const bool want_portal = router.want_portal();
        if (want_portal && !portal_running()) {
            portal_start();
            force = true;
        } else if (!want_portal && portal_running()) {
            portal_stop();
        }
        if (router.page() == ui::Page::Setup) {
            const uint32_t activity = portal_last_activity_ms();
            if (activity != seen_activity) {
                seen_activity = activity;
                router.portal_activity(now);
            }
            ui::Frame probe{};
            router.fill(probe);
            if (probe.setup.stage != ui::SetupStage::Saved)
                router.set_setup_stage(portal_stations() > 0 ? ui::SetupStage::Connected : ui::SetupStage::Waiting, now);
        }

        power_update(now);
        ui::Status next{};
        read_status(next);
        if (next.battery != status.battery || next.charging != status.charging || next.minute != status.minute ||
            next.time_valid != status.time_valid || next.runtime_h != status.runtime_h) {
            status = next;
            portal_set_telemetry(status.battery, status.charging, status.runtime_h);
            force = true;
        }

        if (router.take_scan_event()) badge_scan_count_increment();
        haptics_play(router.take_haptic(), now);
        haptics_update(now);

        if (router.tick(now) || force) {
            force = false;
            ui::Frame frame{};
            router.fill(frame);
            frame.setup.ssid = portal_ssid();
            frame.setup.password = portal_password();
            frame.setup.wifi_qr = portal_wifi_qr();
            frame.setup.url = portal_url();
            renderer.draw(display_canvas(), frame, content, status);
            display_present();
            if (!router.animating()) frame_level = ui::frame_emission(display_canvas());
            const int bad = renderer.take_qr_mismatches();
            if (bad > 0) ESP_LOGW(TAG, "QR readback: %d modules differ from the encoded code", bad);
            else if (bad == 0) ESP_LOGI(TAG, "QR readback OK");
        }
        // Brightness commands share the QSPI bus with the frame flush; pace them during fades.
        const uint8_t target = router.brightness();
        if (target != display_brightness() && (!router.animating() || now - last_brightness_ms >= 16)) {
            display_set_brightness(target);
            last_brightness_ms = now;
        }
        if (now - last_load_ms >= 1000) {
            power_note_load(now, display_brightness(), frame_level, portal_running());
            last_load_ms = now;
        }

        TickType_t delay = router.animating() ? 1 : router.page() == ui::Page::Ambient ? pdMS_TO_TICKS(40)
                                                                                         : pdMS_TO_TICKS(15);
        vTaskDelay(delay);
    }
}
