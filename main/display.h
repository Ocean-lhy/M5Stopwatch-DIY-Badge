#pragma once

#include <stdint.h>

#include <M5GFX.h>

bool display_init();
void display_set_brightness(uint8_t value);
uint8_t display_brightness();
// Full-screen RGB565 back buffer in PSRAM; draw into it, then present.
lgfx::LGFX_Sprite &display_canvas();
void display_present();
