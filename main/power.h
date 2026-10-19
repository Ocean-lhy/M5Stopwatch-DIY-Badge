#pragma once

#include <stdint.h>

struct PowerState {
    int percent;     // -1 when the PM1 cannot be read
    bool charging;   // USB/5V input present
    uint16_t mv;
    float load_ma;   // modelled average board current
    float runtime_h; // remaining hours at load_ma, -1 when unknown
};

// Samples the PM1 at most every few seconds; cheap to call every loop.
void power_update(uint32_t now_ms);
// Feeds the current-consumption model: panel brightness (0..255), the average
// emitted level of the shown frame (0..1) and whether the SoftAP is on.
void power_note_load(uint32_t now_ms, uint8_t brightness, float frame_level, bool wifi);
PowerState power_state();
