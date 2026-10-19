#pragma once

#include <stdint.h>
#include "i2c_bus.h"

struct TouchEvent {
    bool down;
    bool released;
    uint16_t x;
    uint16_t y;
};

bool touch_init(i2c_bus_handle_t bus);
TouchEvent touch_poll();
