#pragma once

#include <stdint.h>

#include "i2c_bus.h"
#include "ui_router.h"

bool input_init(i2c_bus_handle_t bus);

// Polls keys and touch; returns the number of events written to `out`.
int input_poll(uint32_t now_ms, ui::Input *out, int capacity);
