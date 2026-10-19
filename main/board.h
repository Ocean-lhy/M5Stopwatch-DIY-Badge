#pragma once

#include "M5IOE1.h"
#include "M5PM1.h"
#include "i2c_bus.h"

// Shared system I2C (G47/G48) and the two PY32 companions on it.
bool board_init();
i2c_bus_handle_t board_i2c();
M5PM1 *board_pm1();   // null when PM1 did not answer
M5IOE1 *board_ioe1(); // null when the IO expander did not answer
