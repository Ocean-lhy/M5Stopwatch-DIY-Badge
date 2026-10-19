#include "rtc_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"
#include "rx8130.h"

static const char *TAG = "badge_rtc";
static RX8130_Class s_rtc;
static bool s_rtc_ok = false;
static bool s_valid = false;
// 2024-01-01; anything earlier means the clock was never set.
static constexpr time_t kMinValid = 1704067200;

void rtc_time_set_tz(int16_t offset_min)
{
    // POSIX TZ offsets are west-positive, the opposite of the browser's sign.
    char tz[24];
    int west = -offset_min;
    snprintf(tz, sizeof(tz), "LOC%c%d:%02d", west < 0 ? '-' : '+', abs(west) / 60, abs(west) % 60);
    setenv("TZ", tz, 1);
    tzset();
}

void rtc_time_init(i2c_bus_handle_t bus, int16_t offset_min)
{
    rtc_time_set_tz(offset_min);
    s_rtc_ok = bus && s_rtc.begin(bus);
    if (!s_rtc_ok) return;
    struct tm utc = {};
    s_rtc.getTime(&utc);
    if (utc.tm_mon < 0 || utc.tm_mon > 11 || utc.tm_mday < 1 || utc.tm_hour > 23) return;
    // timegm-equivalent: interpret the RTC fields as UTC.
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&utc);
    rtc_time_set_tz(offset_min);
    if (t < kMinValid) {
        ESP_LOGW(TAG, "RTC not set yet");
        return;
    }
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    s_valid = true;
    ESP_LOGI(TAG, "time restored from RTC: %lld", (long long)t);
}

void rtc_time_sync(int64_t epoch_s)
{
    if (epoch_s < kMinValid) return;
    struct timeval tv = {.tv_sec = (time_t)epoch_s, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    s_valid = true;
    if (!s_rtc_ok) return;
    time_t t = (time_t)epoch_s;
    struct tm utc = {};
    gmtime_r(&t, &utc);
    s_rtc.setTime(&utc);
}

bool rtc_time_valid() { return s_valid; }

bool rtc_time_local(struct tm *out)
{
    if (!s_valid) return false;
    time_t now = time(nullptr);
    localtime_r(&now, out);
    return true;
}
