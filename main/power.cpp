#include "power.h"

#include <string.h>
#include <time.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

namespace {

constexpr char TAG[] = "badge_power";
constexpr uint32_t kMagic = 0x31574250; // 'PBW1'
constexpr uint32_t kReadMs = 5000;
constexpr uint32_t kSamplePeriodMs = 5 * 60 * 1000;
constexpr uint32_t kSaveMs = 5 * 60 * 1000;
constexpr uint32_t kFullHoldMs = 60 * 1000;
constexpr float kCapacityMah = 450.0f;
constexpr float kMinCapacity = 280.0f;
constexpr float kMaxCapacity = 520.0f;

// Board current model from M5StopWatch-PowerDemo measurements at 3.7 V:
//   normal_active  45.69 mA  CPU 240 MHz, panel 50 %, mostly black UI, touch/IMU/RTC on
//   display_only  154.45 mA  panel 100 %, full white, other peripherals off
//   wifi_connected 105.73 mA / imu_sampling 36.32 mA, both with the panel off
constexpr float kBaseMa = 44.0f;
constexpr float kPanelWhiteMa = 110.0f;
constexpr float kWifiMa = 69.4f;
// PowerDemo's full_load run (285.77 mA) keeps the motor, speaker and panel on together,
// so there is no measured motor-only current. 90 mA is an allowance for the 0610 motor
// on VBAT; the web page shows the mAh it actually adds.
constexpr float kMotorMa = 90.0f;
// LGS4056 PROG is PMG3. The board pulls it low at boot, selecting 425 mA.
// 0.7 leaves room for CV taper, so a charge is not marked full early.
constexpr float kChargeMa = 425.0f;
constexpr float kChargeFactor = 0.7f;
constexpr float kAverageWindowS = 900.0f;
constexpr float kFullMv = 4100.0f;
constexpr float kLatchMv = 4120.0f;
constexpr float kKneeMv = 3500.0f;
constexpr float kEmptyMv = 3450.0f;
constexpr float kContinueMv = 150.0f;
constexpr float kTraceStepMv = 25.0f;
constexpr time_t kMinValid = 1704067200;

struct CurvePoint {
    uint16_t mv;
    uint8_t pct;
};

// Factory resting curve, used only to seed a discharge before this cell has one of its own.
constexpr CurvePoint kCurve[] = {
    {4180, 100}, {4100, 92}, {4030, 84}, {3970, 75}, {3920, 66}, {3870, 56}, {3830, 46},
    {3800, 37},  {3770, 28}, {3740, 20}, {3700, 13}, {3650, 7},  {3580, 3},  {3450, 0},
};

struct Pt {
    uint16_t mv;
    float used;
};

struct Sample {
    int32_t t;
    uint16_t mv;
    uint16_t ma_x10;
    uint16_t remain_x10;
    uint8_t charging;
};

struct StoredPt {
    uint16_t mv;
    uint16_t used_x10;
};

struct Persist {
    uint32_t magic;
    uint16_t used_x10;
    uint16_t capacity_x10;
    uint16_t motor_x100;
    uint16_t last_mv;
    uint8_t flags;
    uint8_t curve_n;
    uint8_t trace_n;
    uint8_t sample_n;
    uint8_t sample_head;
    uint8_t reserved0;
    uint16_t reserved1;
    int32_t session_t;
    StoredPt curve[kPowerCurveCap];
    StoredPt trace[kPowerCurveCap];
    Sample samples[kPowerHistoryCap];
};

static_assert(sizeof(Persist) < 3500, "power history must fit in one NVS blob");

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
PowerState s_state = {-1, false, 0, 0, -1, 0, kCapacityMah, 0, false, false};
float s_used = 0;
float s_capacity = kCapacityMah;
float s_motor = 0;
float s_filtered = 0;
bool s_seeded = false;
bool s_loaded = false;
bool s_learned = false;
bool s_from_full = false;
bool s_trusted = false; // coulomb count is the source of percent, not the voltage table
bool s_settle = false;  // wait out charge-voltage sag before seeding
bool s_dirty = false;
bool s_force_sample = false;
bool s_full_latched = false;
bool s_announce_learn = false;
uint32_t s_next_read = 0;
uint32_t s_last_load = 0;
uint32_t s_last_integrate = 0;
uint32_t s_next_sample = 0;
uint32_t s_last_save = 0;
uint32_t s_full_since = 0;
uint32_t s_unplug_at = 0;
uint16_t s_last_mv = 0;
int s_curve_n = 0;
int s_trace_n = 0;
int s_sample_n = 0;
int s_sample_head = 0;
Pt s_curve[kPowerCurveCap];
Pt s_trace[kPowerCurveCap];
Sample s_samples[kPowerHistoryCap];
Persist s_save;

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

uint16_t sat_u16(float v)
{
    if (v <= 0) return 0;
    if (v >= 65535.0f) return 65535;
    return (uint16_t)(v + 0.5f);
}

int32_t wall_time()
{
    time_t t = time(nullptr);
    if (t < kMinValid) return 0;
    return (int32_t)t;
}

float used_from_mv(float mv)
{
    if (s_learned && s_curve_n >= 2) {
        if (mv >= s_curve[0].mv) return s_curve[0].used;
        const int last = s_curve_n - 1;
        if (mv <= s_curve[last].mv) {
            // A trace that reached the knee is the whole capacity. A trace that
            // stopped higher up does not justify extrapolating its slope.
            if (s_curve[last].mv <= kKneeMv + 40) return s_capacity;
            float factory = s_capacity * (100 - percent_from_mv(mv)) / 100.0f;
            return factory > s_curve[last].used ? factory : s_curve[last].used;
        }
        for (int i = 1; i < s_curve_n; ++i) {
            if (mv >= s_curve[i].mv) {
                float dv = (float)s_curve[i - 1].mv - (float)s_curve[i].mv;
                float t = dv > 0 ? ((float)s_curve[i - 1].mv - mv) / dv : 0;
                return s_curve[i - 1].used + t * (s_curve[i].used - s_curve[i - 1].used);
            }
        }
    }
    return s_capacity * (100 - percent_from_mv(mv)) / 100.0f;
}

void publish()
{
    s_state.used_mah = s_used;
    s_state.capacity_mah = s_capacity;
    s_state.motor_mah = s_motor;
    s_state.learned = s_learned;
    s_state.from_full = s_from_full;
    if (!s_seeded || !s_trusted || s_state.charging) {
        s_state.runtime_h = -1;
        return;
    }
    float remain = s_capacity - s_used;
    if (remain < 0) remain = 0;
    if (s_filtered >= 2500 && s_filtered <= kEmptyMv) remain = 0;
    int pct = s_capacity > 0 ? (int)(remain / s_capacity * 100.0f + 0.5f) : 0;
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    s_state.percent = pct;
    s_state.runtime_h = s_state.load_ma > 0 ? remain / s_state.load_ma : -1;
}

void push_sample()
{
    float remain = s_capacity - s_used;
    if (remain < 0) remain = 0;
    if (!s_state.charging && s_filtered >= 2500 && s_filtered <= kEmptyMv) remain = 0;
    float hours = s_state.load_ma > 1 ? remain / s_state.load_ma : 0;
    Sample s{};
    s.t = wall_time();
    s.mv = sat_u16(s_filtered);
    s.ma_x10 = sat_u16(s_state.load_ma * 10.0f);
    s.remain_x10 = sat_u16(hours * 10.0f);
    s.charging = s_state.charging ? 1 : 0;
    s_samples[s_sample_head] = s;
    s_sample_head = (s_sample_head + 1) % kPowerHistoryCap;
    if (s_sample_n < kPowerHistoryCap) ++s_sample_n;
    s_dirty = true;
}

void decimate_trace()
{
    Pt tmp[kPowerCurveCap];
    int w = 0;
    for (int i = 0; i < s_trace_n; i += 2) tmp[w++] = s_trace[i];
    if (w > 0 && s_trace[s_trace_n - 1].mv != tmp[w - 1].mv) tmp[w++] = s_trace[s_trace_n - 1];
    memcpy(s_trace, tmp, (size_t)w * sizeof(Pt));
    s_trace_n = w;
}

void push_trace(float mv, float used)
{
    if (s_trace_n > 0) {
        if (mv > (float)s_trace[s_trace_n - 1].mv - kTraceStepMv) return;
        if (used + 0.05f < s_trace[s_trace_n - 1].used) return;
    }
    if (s_trace_n >= kPowerCurveCap) decimate_trace();
    if (s_trace_n >= kPowerCurveCap) return;
    s_trace[s_trace_n++] = {sat_u16(mv), used};
    s_dirty = true;
}

void commit_trace(bool set_capacity)
{
    if (s_trace_n < 6) return;
    float span = (float)s_trace[0].mv - (float)s_trace[s_trace_n - 1].mv;
    if (span < 150) return;
    float old_span = s_curve_n >= 2 ? (float)s_curve[0].mv - (float)s_curve[s_curve_n - 1].mv : 0;
    bool lower = s_curve_n >= 1 && s_trace[s_trace_n - 1].mv + 10 < s_curve[s_curve_n - 1].mv;
    if (s_curve_n >= 6 && span + 20 < old_span && !lower && !set_capacity) return;
    memcpy(s_curve, s_trace, (size_t)s_trace_n * sizeof(Pt));
    s_curve_n = s_trace_n;
    s_learned = true;
    if (set_capacity) {
        float cap = s_used;
        if (cap < kMinCapacity) cap = kMinCapacity;
        if (cap > kMaxCapacity) cap = kMaxCapacity;
        s_capacity = cap;
        s_from_full = false;
    }
    s_dirty = true;
    s_announce_learn = true;
}

void maybe_trace()
{
    if (!s_from_full || s_state.charging || s_filtered < 2500) return;
    if (s_trace_n == 0) push_trace(s_filtered, s_used);
    else push_trace(s_filtered, s_used);
    if (s_filtered <= kKneeMv && s_used >= 150) commit_trace(true);
}

bool curve_monotonic(const StoredPt *p, int n)
{
    for (int i = 1; i < n; ++i) {
        if (p[i].mv >= p[i - 1].mv) return false;
        if (p[i].used_x10 < p[i - 1].used_x10) return false;
    }
    return true;
}

bool persist_ok(const Persist &p)
{
    if (p.magic != kMagic) return false;
    if (p.curve_n > kPowerCurveCap || p.trace_n > kPowerCurveCap) return false;
    if (p.sample_n > kPowerHistoryCap || p.sample_head >= kPowerHistoryCap) return false;
    if (!curve_monotonic(p.curve, p.curve_n) || !curve_monotonic(p.trace, p.trace_n)) return false;
    int start = p.sample_n < kPowerHistoryCap ? 0 : p.sample_head;
    for (int i = 0; i < p.sample_n; ++i) {
        const Sample &s = p.samples[(start + i) % kPowerHistoryCap];
        if (s.mv < 2500 || s.mv > 4500) return false;
    }
    return true;
}

void load_persist()
{
    nvs_handle_t handle = 0;
    if (nvs_open("power", NVS_READONLY, &handle) != ESP_OK) return;
    size_t len = sizeof(s_save);
    esp_err_t err = nvs_get_blob(handle, "log", &s_save, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(s_save) || !persist_ok(s_save)) return;

    s_used = s_save.used_x10 / 10.0f;
    s_capacity = s_save.capacity_x10 / 10.0f;
    if (s_capacity < kMinCapacity || s_capacity > kMaxCapacity) s_capacity = kCapacityMah;
    s_motor = s_save.motor_x100 / 100.0f;
    s_last_mv = s_save.last_mv;
    s_from_full = (s_save.flags & 1) != 0;
    s_learned = (s_save.flags & 2) != 0;
    s_curve_n = s_save.curve_n;
    s_trace_n = s_save.trace_n;
    if (s_learned && s_curve_n < 2) s_learned = false;
    for (int i = 0; i < s_curve_n; ++i)
        s_curve[i] = {s_save.curve[i].mv, s_save.curve[i].used_x10 / 10.0f};
    for (int i = 0; i < s_trace_n; ++i)
        s_trace[i] = {s_save.trace[i].mv, s_save.trace[i].used_x10 / 10.0f};
    s_sample_n = s_save.sample_n;
    s_sample_head = s_save.sample_head;
    memcpy(s_samples, s_save.samples, sizeof(s_samples));
    s_loaded = true;
    ESP_LOGI(TAG, "restored used=%d cap=%d samples=%d", (int)(s_used + 0.5f), (int)(s_capacity + 0.5f),
             s_sample_n);
}

void pack(Persist *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = kMagic;
    out->used_x10 = sat_u16(s_used * 10.0f);
    out->capacity_x10 = sat_u16(s_capacity * 10.0f);
    out->motor_x100 = sat_u16(s_motor * 100.0f);
    out->last_mv = s_filtered >= 2500 ? sat_u16(s_filtered) : s_last_mv;
    out->flags = (uint8_t)((s_from_full ? 1 : 0) | (s_learned ? 2 : 0));
    out->curve_n = (uint8_t)s_curve_n;
    out->trace_n = (uint8_t)s_trace_n;
    out->sample_n = (uint8_t)s_sample_n;
    out->sample_head = (uint8_t)s_sample_head;
    out->session_t = wall_time();
    for (int i = 0; i < s_curve_n; ++i) {
        out->curve[i].mv = s_curve[i].mv;
        out->curve[i].used_x10 = sat_u16(s_curve[i].used * 10.0f);
    }
    for (int i = 0; i < s_trace_n; ++i) {
        out->trace[i].mv = s_trace[i].mv;
        out->trace[i].used_x10 = sat_u16(s_trace[i].used * 10.0f);
    }
    memcpy(out->samples, s_samples, sizeof(s_samples));
}

void maybe_save(uint32_t now, bool force)
{
    if (!s_seeded) return;
    if (!force && !s_dirty) return;
    if (!force && s_last_save && (uint32_t)(now - s_last_save) < kSaveMs) return;
    portENTER_CRITICAL(&s_mux);
    pack(&s_save);
    s_dirty = false;
    portEXIT_CRITICAL(&s_mux);

    nvs_handle_t handle = 0;
    if (nvs_open("power", NVS_READWRITE, &handle) != ESP_OK) {
        s_dirty = true;
        return;
    }
    esp_err_t err = nvs_set_blob(handle, "log", &s_save, sizeof(s_save));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs save failed: %s", esp_err_to_name(err));
        s_dirty = true;
        return;
    }
    s_last_save = now;
}

void seed(float mv, bool charging)
{
    float delta = mv > s_last_mv ? mv - s_last_mv : s_last_mv - mv;
    bool cont = s_loaded && s_last_mv >= 2500 && delta < kContinueMv;
    if (cont) {
        // Same discharge, including a reboot. The integral already in NVS stays.
        s_trusted = true;
    } else if (charging) {
        // Charger voltage sits above the resting curve. Don't treat it as full
        // until it has held 4.12 V, or until 30 s after unplug.
        s_used = used_from_mv(mv);
        s_from_full = false;
        s_trusted = false;
    } else {
        s_used = used_from_mv(mv);
        s_from_full = s_used < 8.0f;
        s_trusted = true;
        if (s_from_full) {
            s_trace_n = 0;
            push_trace(mv, s_used);
        }
    }
    s_seeded = true;
    s_last_integrate = 0;
    s_dirty = true;
}

void finish_settle()
{
    s_used = used_from_mv(s_filtered);
    s_from_full = s_used < 8.0f;
    s_trusted = true;
    s_settle = false;
    if (s_from_full) {
        s_trace_n = 0;
        push_trace(s_filtered, s_used);
    }
    s_force_sample = true;
    s_dirty = true;
}

void on_unplug(uint32_t now, float mv)
{
    // A high reading only counts as full once this session was already tracking coulombs.
    // Right after a charge the terminal voltage sags for a while; that case settles below.
    if (s_full_latched || (s_trusted && mv >= kFullMv)) {
        s_used = 0;
        s_from_full = true;
        s_trusted = true;
        s_settle = false;
        s_trace_n = 0;
        push_trace(mv, 0);
    } else if (s_trusted) {
        if (s_used < 1.0f) s_used = used_from_mv(mv);
        s_settle = false;
    } else {
        s_settle = true;
        s_unplug_at = now ? now : 1;
        s_trusted = false;
    }
    s_full_latched = false;
    s_full_since = 0;
    s_force_sample = true;
    s_dirty = true;
}

void on_plug()
{
    // Charging breaks a voltage-versus-used trace. Coulomb credit keeps running.
    if (s_from_full) commit_trace(false);
    s_trace_n = 0;
    s_from_full = false;
    s_full_since = 0;
    s_dirty = true;
}

void note_full_hold(uint32_t now, bool charging, float mv)
{
    if (charging && mv >= kLatchMv) {
        if (!s_full_since) s_full_since = now ? now : 1;
        else if ((uint32_t)(now - s_full_since) >= kFullHoldMs) s_full_latched = true;
    } else {
        s_full_since = 0;
    }
}

void integrate(uint32_t now, float ma)
{
    if (!s_seeded) {
        s_last_integrate = now ? now : 1;
        return;
    }
    uint32_t dt_ms = s_last_integrate ? (uint32_t)(now - s_last_integrate) : 0;
    s_last_integrate = now ? now : 1;
    if (!s_last_integrate || dt_ms == 0 || dt_ms >= 30000) return;
    float dt_h = (dt_ms / 1000.0f) / 3600.0f;
    if (!s_trusted) return;
    if (s_state.charging) {
        // Charger current is shared with the board. Anything above it still comes from the cell.
        if (ma >= kChargeMa) s_used += (ma - kChargeMa) * dt_h;
        else {
            s_used -= (kChargeMa - ma) * kChargeFactor * dt_h;
            if (s_used < 0) s_used = 0;
        }
        return;
    }
    s_used += ma * dt_h;
    if (s_used >= s_capacity && s_filtered > 3680) {
        s_capacity = s_used + 15.0f;
        if (s_capacity > kMaxCapacity) s_capacity = kMaxCapacity;
    }
}

void smooth(uint32_t now, float ma)
{
    if (s_last_load == 0) s_state.load_ma = ma;
    else {
        float k = (now - s_last_load) / 1000.0f / kAverageWindowS;
        if (k > 1) k = 1;
        if (k < 0) k = 0;
        s_state.load_ma += (ma - s_state.load_ma) * k;
    }
    s_last_load = now ? now : 1;
}

void maybe_sample(uint32_t now)
{
    if (!s_seeded || s_state.load_ma < 1) return;
    if (!s_force_sample && s_next_sample && (int32_t)(now - s_next_sample) < 0) return;
    s_force_sample = false;
    s_next_sample = now + kSamplePeriodMs;
    push_sample();
}

} // namespace

void power_note_motor(uint32_t on_ms)
{
    if (on_ms == 0 || on_ms > 500) return;
    float mah = kMotorMa * (on_ms / 3600000.0f);
    portENTER_CRITICAL(&s_mux);
    s_motor += mah;
    if (s_seeded && s_trusted && !s_state.charging) s_used += mah;
    s_dirty = true;
    publish();
    portEXIT_CRITICAL(&s_mux);
}

void power_note_load(uint32_t now, uint8_t brightness, float frame_level, bool wifi)
{
    if (frame_level < 0) frame_level = 0;
    if (frame_level > 1) frame_level = 1;
    const float ma = kBaseMa + kPanelWhiteMa * (brightness / 255.0f) * frame_level + (wifi ? kWifiMa : 0.0f);
    portENTER_CRITICAL(&s_mux);
    integrate(now, ma);
    smooth(now, ma);
    publish();
    maybe_sample(now);
    portEXIT_CRITICAL(&s_mux);
    maybe_save(now, false);
}

void power_update(uint32_t now)
{
    static bool tried_load = false;
    if (!tried_load) {
        tried_load = true;
        load_persist();
    }
    if (s_next_read && (int32_t)(now - s_next_read) < 0) return;
    s_next_read = now + kReadMs;

    M5PM1 *pm = board_pm1();
    if (!pm) return;
    uint16_t raw = 0;
    if (pm->readVbat(&raw) != M5PM1_OK || raw < 2500) {
        if (!s_seeded) {
            portENTER_CRITICAL(&s_mux);
            s_state.percent = -1;
            s_state.runtime_h = -1;
            portEXIT_CRITICAL(&s_mux);
        }
        return;
    }
    m5pm1_pwr_src_t src = M5PM1_PWR_SRC_UNKNOWN;
    bool charging = pm->getPowerSource(&src) == M5PM1_OK &&
                    (src == M5PM1_PWR_SRC_5VIN || src == M5PM1_PWR_SRC_5VINOUT);
    float mv = raw;

    portENTER_CRITICAL(&s_mux);
    bool was = s_state.charging;
    if (s_filtered == 0 || charging != was) s_filtered = mv;
    else s_filtered += (mv - s_filtered) * 0.25f;
    s_state.mv = sat_u16(s_filtered);

    if (!s_seeded) {
        seed(s_filtered, charging);
        s_state.charging = charging;
        if (charging) s_state.percent = percent_from_mv(s_filtered);
        else publish();
        note_full_hold(now, charging, s_filtered);
        int used_i = (int)(s_used + 0.5f);
        int cap_i = (int)(s_capacity + 0.5f);
        bool learned = s_learned;
        bool full = s_from_full;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "seed mv=%d used=%d cap=%d learned=%d full=%d", (int)mv, used_i, cap_i, learned, full);
        maybe_save(now, true);
        return;
    }

    if (s_settle && !charging && s_unplug_at && (uint32_t)(now - s_unplug_at) >= 30000) finish_settle();
    if (charging != was) {
        if (was && !charging) on_unplug(now, s_filtered);
        else if (!was && charging) on_plug();
    }
    s_state.charging = charging;
    note_full_hold(now, charging, s_filtered);
    if (charging || !s_trusted) {
        int pct = percent_from_mv(s_filtered);
        if (charging && s_state.percent >= 0 && pct < s_state.percent) pct = s_state.percent;
        s_state.percent = pct;
        s_state.runtime_h = -1;
        s_state.used_mah = s_used;
        s_state.capacity_mah = s_capacity;
        s_state.motor_mah = s_motor;
        s_state.learned = s_learned;
        s_state.from_full = s_from_full;
    } else {
        maybe_trace();
        publish();
    }
    bool announce = s_announce_learn;
    int points = s_curve_n;
    int cap_i = (int)(s_capacity + 0.5f);
    s_announce_learn = false;
    portEXIT_CRITICAL(&s_mux);
    if (announce) ESP_LOGI(TAG, "learned points=%d cap=%d", points, cap_i);
    maybe_save(now, false);
}

PowerState power_state()
{
    portENTER_CRITICAL(&s_mux);
    PowerState copy = s_state;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

int power_copy_samples(PowerSample *out, int max)
{
    if (!out || max <= 0) return 0;
    portENTER_CRITICAL(&s_mux);
    int n = s_sample_n < max ? s_sample_n : max;
    int start = n > 0 ? (s_sample_head - n + kPowerHistoryCap) % kPowerHistoryCap : 0;
    for (int i = 0; i < n; ++i) {
        const Sample &s = s_samples[(start + i) % kPowerHistoryCap];
        out[i] = {s.t, s.mv, s.ma_x10, s.remain_x10, s.charging != 0};
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

int power_copy_learned(PowerUsedPoint *out, int max)
{
    if (!out || max <= 0) return 0;
    portENTER_CRITICAL(&s_mux);
    int n = s_learned ? s_curve_n : 0;
    if (n > max) n = max;
    for (int i = 0; i < n; ++i) out[i] = {s_curve[i].mv, sat_u16(s_curve[i].used * 10.0f)};
    portEXIT_CRITICAL(&s_mux);
    return n;
}

int power_copy_factory(PowerVoltPoint *out, int max)
{
    const int count = (int)(sizeof(kCurve) / sizeof(kCurve[0]));
    int n = count < max ? count : max;
    if (!out || n <= 0) return 0;
    for (int i = 0; i < n; ++i) out[i] = {kCurve[i].mv, kCurve[i].pct};
    return n;
}

void power_clear_calibration()
{
    portENTER_CRITICAL(&s_mux);
    s_learned = false;
    s_curve_n = 0;
    s_trace_n = 0;
    s_from_full = false;
    s_announce_learn = false;
    s_capacity = kCapacityMah;
    if (s_filtered >= 2500.0f) {
        s_used = used_from_mv(s_filtered);
        s_seeded = true;
        if (!s_state.charging) s_trusted = true;
    }
    s_dirty = true;
    if (s_state.charging || !s_trusted) {
        if (s_filtered >= 2500.0f) s_state.percent = percent_from_mv(s_filtered);
        s_state.runtime_h = -1;
        s_state.used_mah = s_used;
        s_state.capacity_mah = s_capacity;
        s_state.learned = false;
        s_state.from_full = false;
    } else {
        publish();
    }
    portEXIT_CRITICAL(&s_mux);
    maybe_save(0, true);
    ESP_LOGI(TAG, "calibration cleared");
}
