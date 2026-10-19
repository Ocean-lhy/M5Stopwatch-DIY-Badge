#include "display.h"

#include "driver/gpio.h"
#include "orient.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lgfx/v1/panel/Panel_CO5300.hpp"
#include "ui_theme.h"

static const char *TAG = "badge_display";
static constexpr gpio_num_t kSclk = GPIO_NUM_40;
static constexpr gpio_num_t kD0 = GPIO_NUM_41;
static constexpr gpio_num_t kD1 = GPIO_NUM_42;
static constexpr gpio_num_t kD2 = GPIO_NUM_46;
static constexpr gpio_num_t kD3 = GPIO_NUM_45;
static constexpr gpio_num_t kCs = GPIO_NUM_39;
static constexpr gpio_num_t kTe = GPIO_NUM_38;

// Panel setup taken from M5StopWatch-PowerDemo (modules/m5sw_display), which is
// validated on production StopWatch hardware with this M5GFX revision.
class StopwatchPanel final : public lgfx::Panel_CO5300 {
public:
    StopwatchPanel()
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t init[] = {
            0x11, 0 + CMD_INIT_DELAY, 150,
            0xC4, 1, 0x80,
            0x35, 1, 0x80,
            0x44, 2, 0x01, 0xD2,
            0x53, 1, 0x20,
            0x20, 0,
            0x36, 1, 0x00,
            0x51, 1, 0x00,
            0x29, 0,
            0xFF, 0xFF,
        };
        return listno == 0 ? init : nullptr;
    }
};

class BadgeGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI bus_;
    StopwatchPanel panel_;

public:
    BadgeGFX()
    {
        auto bus_cfg = bus_.config();
        bus_cfg.freq_write = 80000000;
        bus_cfg.freq_read = 10000000;
        bus_cfg.pin_sclk = kSclk;
        bus_cfg.pin_mosi = GPIO_NUM_NC;
        bus_cfg.pin_miso = GPIO_NUM_NC;
        bus_cfg.pin_io0 = kD0;
        bus_cfg.pin_io1 = kD1;
        bus_cfg.pin_io2 = kD2;
        bus_cfg.pin_io3 = kD3;
        bus_cfg.spi_host = SPI2_HOST;
        bus_cfg.spi_mode = 0;
        bus_cfg.spi_3wire = true;
        bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
        bus_.config(bus_cfg);
        panel_.setBus(&bus_);

        auto panel_cfg = panel_.config();
        panel_cfg.pin_cs = kCs;
        panel_cfg.pin_rst = GPIO_NUM_NC;
        panel_cfg.pin_busy = GPIO_NUM_NC;
        panel_cfg.panel_width = 468;
        panel_cfg.panel_height = ui::kSize;
        panel_cfg.offset_x = 6;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 0;
        panel_cfg.readable = false;
        panel_cfg.invert = false;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);
        panel_.setRotation(0);
        setPanel(&panel_);
    }

    bool enable_framebuffer()
    {
        if (!panel_.initPanelFb()) return false;
        auto *fb = panel_.getPanelFb();
        if (!fb) return false;
        fb->setBus(&bus_);
        fb->setAutoDisplay(true);
        setPanel(fb);
        return true;
    }

    // The framebuffer flush leaves its last DMA line in flight; a brightness
    // command must not reconfigure the bus until that transfer has finished.
    void brightness(uint8_t value)
    {
        bus_.wait();
        panel_.setBrightness(value);
    }
};

static BadgeGFX gfx;
static M5Canvas canvas(&gfx);
static lgfx::LGFX_Sprite rotated;
static uint8_t current_brightness = 0;
static uint8_t current_rotation = 0;

bool display_init()
{
    gpio_config_t te = {};
    te.pin_bit_mask = 1ULL << kTe;
    te.mode = GPIO_MODE_INPUT;
    te.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&te);

    if (!gfx.init() || !gfx.enable_framebuffer()) {
        ESP_LOGE(TAG, "CO5300 init failed");
        return false;
    }
    gfx.setRotation(0);
    gfx.fillScreen(TFT_BLACK);
    gfx.brightness(0);

    canvas.setColorDepth(16);
    if (!canvas.createSprite(ui::kSize, ui::kSize)) {
        ESP_LOGE(TAG, "framebuffer allocation failed, free PSRAM=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return false;
    }
    canvas.setTextWrap(false);
    canvas.fillSprite(TFT_BLACK);
    return true;
}

void display_set_brightness(uint8_t value)
{
    if (value == current_brightness) return;
    current_brightness = value;
    gfx.brightness(value);
}

uint8_t display_brightness() { return current_brightness; }

void display_set_rotation(uint8_t quarter) { current_rotation = quarter & 3; }

lgfx::LGFX_Sprite &display_canvas() { return canvas; }

void display_present()
{
    // One transaction per frame so auto-display flushes the whole frame once.
    gfx.startWrite();
    if (current_rotation == 0) {
        canvas.pushSprite(0, 0);
    } else {
        if (!rotated.getBuffer()) {
            rotated.setPsram(true);
            rotated.setColorDepth(16);
            rotated.createSprite(ui::kSize, ui::kSize);
            if (!rotated.getBuffer()) ESP_LOGW(TAG, "rotation buffer unavailable");
        }
        const auto *src = static_cast<const uint16_t *>(canvas.getBuffer());
        auto *dst = static_cast<uint16_t *>(rotated.getBuffer());
        if (src && dst) {
            orient_blit(src, dst, ui::kSize, current_rotation);
            // This sprite has no parent; pushSprite(x, y) would dereference a null device.
            rotated.pushSprite(&gfx, 0, 0);
        } else {
            canvas.pushSprite(0, 0);
        }
    }
    gfx.endWrite();
}
