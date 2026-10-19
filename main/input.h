#pragma once

#include <stdint.h>

#include "ui_router.h"

// Keys only. The touch controller stays in reset and is not read.
bool input_init();

// Polls the two keys; returns the number of events written to `out`.
int input_poll(uint32_t now_ms, ui::Input *out, int capacity);
