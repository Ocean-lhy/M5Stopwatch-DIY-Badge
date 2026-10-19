#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "badge_board";
static constexpr int kI2cSda = 47;
static constexpr int kI2cScl = 48;

static i2c_bus_handle_t s_bus = nullptr;
static M5PM1 s_pm1;
static M5IOE1 s_ioe1;
static bool s_pm1_ok = false;
static bool s_ioe1_ok = false;

bool board_init()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = static_cast<gpio_num_t>(kI2cSda),
        .scl_io_num = static_cast<gpio_num_t>(kI2cScl),
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 100000},
        .clk_flags = 0,
    };
    s_bus = i2c_bus_create(I2C_NUM_0, &conf);
    if (!s_bus) {
        ESP_LOGE(TAG, "I2C bus create failed");
        return false;
    }
    s_pm1_ok = s_pm1.begin(s_bus, M5PM1_DEFAULT_ADDR, 100000) == M5PM1_OK;
    if (!s_pm1_ok) ESP_LOGW(TAG, "PM1 init failed; battery unavailable");
    else if (s_pm1.setLedEnLevel(false) != M5PM1_OK) ESP_LOGW(TAG, "failed to turn PM1 LED off");

    // Stopwatch's PY32 expander uses the second M5IOE1 address.
    s_ioe1_ok = s_ioe1.begin(s_bus, M5IOE1_DEFAULT_ADDR_2, 100000) == M5IOE1_OK;
    if (s_ioe1_ok) {
        // Same power-up order as the M5StopWatch-PowerDemo display module.
        s_ioe1.pinMode(M5IOE1_PIN_5, OUTPUT); // PYG5: AMOLED reset
        s_ioe1.digitalWrite(M5IOE1_PIN_5, HIGH);
        s_ioe1.pinMode(M5IOE1_PIN_8, OUTPUT); // PYG8: 3V3_L3B display rail
        s_ioe1.digitalWrite(M5IOE1_PIN_8, HIGH);
        vTaskDelay(pdMS_TO_TICKS(10));
        s_ioe1.digitalWrite(M5IOE1_PIN_5, LOW);
        vTaskDelay(pdMS_TO_TICKS(8));
        s_ioe1.digitalWrite(M5IOE1_PIN_5, HIGH);
        vTaskDelay(pdMS_TO_TICKS(2));
        s_ioe1.pinMode(M5IOE1_PIN_9, OUTPUT); // PYG9: vibration motor
        s_ioe1.digitalWrite(M5IOE1_PIN_9, LOW);
        // Hold the CST820 in reset so it is never clocked or read.
        s_ioe1.pinMode(M5IOE1_PIN_4, OUTPUT);
        s_ioe1.digitalWrite(M5IOE1_PIN_4, LOW);
    } else {
        ESP_LOGW(TAG, "IO expander init failed; display rail may be off");
    }
    return true;
}

i2c_bus_handle_t board_i2c() { return s_bus; }
M5PM1 *board_pm1() { return s_pm1_ok ? &s_pm1 : nullptr; }
M5IOE1 *board_ioe1() { return s_ioe1_ok ? &s_ioe1 : nullptr; }
