#include "touch.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "badge_touch";
static constexpr uint8_t kAddress = 0x15;
static constexpr uint8_t kStatus = 0x00;
static constexpr uint8_t kReportLength = 13;
static i2c_bus_device_handle_t device = nullptr;
static bool was_down = false;

bool touch_init(i2c_bus_handle_t bus)
{
    if (!bus) return false;
    device = i2c_bus_device_create(bus, kAddress, 100000);
    if (!device) return false;
    uint8_t chip_info[4] = {};
    if (i2c_bus_read_bytes(device, 0xA7, sizeof(chip_info), chip_info) != ESP_OK) {
        ESP_LOGW(TAG, "CST820 not responding");
        return false;
    }
    ESP_LOGI(TAG, "CST820 chip=0x%02x project=0x%02x fw=0x%02x", chip_info[0], chip_info[1], chip_info[2]);
    return true;
}

TouchEvent touch_poll()
{
    TouchEvent event{};
    if (!device) return event;
    uint8_t buf[kReportLength] = {};
    if (i2c_bus_read_bytes(device, kStatus, sizeof(buf), buf) != ESP_OK) return event;
    uint8_t fingers = buf[2];
    bool down = fingers > 0;
    event.down = down;
    event.released = was_down && !down;
    if (down) {
        event.x = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];
        event.y = ((uint16_t)(buf[5] & 0x0F) << 8) | buf[6];
        if (event.x > 465) event.x = 465;
        if (event.y > 465) event.y = 465;
    }
    was_down = down;
    return event;
}
