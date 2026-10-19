#pragma once

#include <stdint.h>

#include "ui_router.h"

void haptics_play(ui::Haptic pattern, uint32_t now_ms);
// Call every loop iteration; switches the motor according to the active pattern.
void haptics_update(uint32_t now_ms);
