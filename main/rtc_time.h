#pragma once

#include <stdint.h>
#include <time.h>

#include "i2c_bus.h"

// The RTC holds UTC; the local offset comes from the browser at sync time.
void rtc_time_init(i2c_bus_handle_t bus, int16_t tz_offset_min);
void rtc_time_set_tz(int16_t tz_offset_min);
// Sets the system clock and writes it back to the RX8130.
void rtc_time_sync(int64_t epoch_s);
bool rtc_time_valid();
bool rtc_time_local(struct tm *out);
