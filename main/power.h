#pragma once

#include <stdint.h>

constexpr int kPowerHistoryCap = 144; // 12 h at one point every 5 min
constexpr int kPowerCurveCap = 24;

struct PowerState {
    int percent;     // coulomb percent while discharging, -1 when the PM1 cannot be read
    bool charging;   // USB/5V input present
    uint16_t mv;
    float load_ma;   // modelled average board current, used as the forecast
    float runtime_h; // remaining hours at load_ma, -1 when unknown or charging
    float used_mah;  // integrated since the last full-charge anchor
    float capacity_mah;
    float motor_mah; // vibration pulses accounted on top of the board model
    bool learned;    // a discharge curve from this cell has been stored
    bool from_full;  // this discharge started from a full-charge anchor
};

struct PowerSample {
    int32_t t; // unix seconds, 0 when the clock is not set
    uint16_t mv;
    uint16_t ma_x10;
    uint16_t remain_x10; // remaining hours * 10 at the time of the sample
    bool charging;
};

struct PowerUsedPoint {
    uint16_t mv;
    uint16_t used_x10;
};

struct PowerVoltPoint {
    uint16_t mv;
    uint8_t pct;
};

// Samples the PM1 at most every few seconds; cheap to call every loop.
void power_update(uint32_t now_ms);
// Feeds the current-consumption model: panel brightness (0..255), the average
// emitted level of the shown frame (0..1) and whether the SoftAP is on.
// Past energy integrates this instantaneous current. runtime_h uses the
// 15-minute average as the forecast.
void power_note_load(uint32_t now_ms, uint8_t brightness, float frame_level, bool wifi);
// Adds a completed motor on-pulse. Pulses longer than 0.5 s are ignored.
void power_note_motor(uint32_t on_ms);

PowerState power_state();
// Copies are chronological (oldest first). Returns the number written.
int power_copy_samples(PowerSample *out, int max);
int power_copy_learned(PowerUsedPoint *out, int max);
int power_copy_factory(PowerVoltPoint *out, int max);
// Drops the learned voltage curve and capacity. History samples stay.
// Returns false when the reset could not be persisted to NVS.
bool power_clear_calibration();
