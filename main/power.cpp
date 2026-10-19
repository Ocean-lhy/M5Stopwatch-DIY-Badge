#include "power.h"

#include "board.h"

namespace {

constexpr uint32_t kSampleMs = 5000;
constexpr float kCapacityMah = 450.0f;

// Board current model from M5StopWatch-PowerDemo measurements at 3.7 V:
//   normal_active  45.69 mA  CPU 240 MHz, panel 50 %, mostly black UI, touch/IMU/RTC on
//   display_only  154.45 mA  panel 100 %, full white, other peripherals off
//   wifi_connected 105.73 mA / imu_sampling 36.32 mA, both with the panel off
constexpr float kBaseMa = 44.0f;       // normal_active minus its small panel emission share
constexpr float kPanelWhiteMa = 110.0f; // display_only minus the active base
constexpr float kWifiMa = 69.4f;        // wifi_connected minus imu_sampling
// Long enough to average a mix of ambient, card and scan pages.
constexpr float kAverageWindowS = 900.0f;

struct CurvePoint {
    uint16_t mv;
    uint8_t pct;
};

// Typical single-cell LiPo resting curve under light load.
constexpr CurvePoint kCurve[] = {
    {4180, 100}, {4100, 92}, {4030, 84}, {3970, 75}, {3920, 66}, {3870, 56}, {3830, 46},
    {3800, 37},  {3770, 28}, {3740, 20}, {3700, 13}, {3650, 7},  {3580, 3},  {3450, 0},
};

PowerState s_state = {-1, false, 0, 0, -1};
uint32_t s_next = 0;
float s_filtered = 0;
uint32_t s_last_load = 0;

int percent_from_mv(float mv)
{
    const int count = sizeof(kCurve) / sizeof(kCurve[0]);
    if (mv >= kCurve[0].mv) return 100;
    for (int i = 1; i < count; ++i) {
        if (mv >= kCurve[i].mv) {
            const CurvePoint &hi = kCurve[i - 1];
            const CurvePoint &lo = kCurve[i];
            float t = (mv - lo.mv) / (float)(hi.mv - lo.mv);
            return (int)(lo.pct + t * (hi.pct - lo.pct) + 0.5f);
        }
    }
    return 0;
}

void update_runtime()
{
    if (s_state.percent < 0 || s_state.charging || s_state.load_ma <= 0) {
        s_state.runtime_h = -1;
        return;
    }
    s_state.runtime_h = kCapacityMah * s_state.percent / 100.0f / s_state.load_ma;
}

} // namespace

void power_note_load(uint32_t now, uint8_t brightness, float frame_level, bool wifi)
{
    const float ma = kBaseMa + kPanelWhiteMa * (brightness / 255.0f) * frame_level + (wifi ? kWifiMa : 0.0f);
    if (s_last_load == 0) {
        s_state.load_ma = ma;
    } else {
        float k = (now - s_last_load) / 1000.0f / kAverageWindowS;
        if (k > 1) k = 1;
        s_state.load_ma += (ma - s_state.load_ma) * k;
    }
    s_last_load = now ? now : 1;
    update_runtime();
}

void power_update(uint32_t now)
{
    if (s_next && (int32_t)(now - s_next) < 0) return;
    s_next = now + kSampleMs;
    M5PM1 *pm = board_pm1();
    if (!pm) return;

    uint16_t mv = 0;
    if (pm->readVbat(&mv) != M5PM1_OK || mv < 2500) {
        s_state.percent = -1;
        update_runtime();
        return;
    }
    m5pm1_pwr_src_t src = M5PM1_PWR_SRC_UNKNOWN;
    bool charging = pm->getPowerSource(&src) == M5PM1_OK &&
                    (src == M5PM1_PWR_SRC_5VIN || src == M5PM1_PWR_SRC_5VINOUT);

    // Charging lifts the terminal voltage, so restart the filter on transitions.
    if (s_filtered == 0 || charging != s_state.charging) s_filtered = mv;
    else s_filtered += (mv - s_filtered) * 0.25f;

    int pct = percent_from_mv(s_filtered);
    // Avoid the displayed value bouncing up while discharging.
    if (!charging && s_state.percent >= 0 && !s_state.charging && pct > s_state.percent && pct - s_state.percent < 3)
        pct = s_state.percent;
    s_state.percent = pct;
    s_state.charging = charging;
    s_state.mv = mv;
    update_runtime();
}

PowerState power_state() { return s_state; }
