#include "haptics.h"

#include "board.h"

namespace {

// Alternating on/off durations in ms, starting with "on".
constexpr uint16_t kTick[] = {28};
constexpr uint16_t kDouble[] = {30, 70, 30};

const uint16_t *s_steps = nullptr;
int s_count = 0;
int s_index = 0;
uint32_t s_step_start = 0;
bool s_on = false;

void motor(bool on)
{
    if (s_on == on) return;
    if (M5IOE1 *io = board_ioe1()) io->digitalWrite(M5IOE1_PIN_9, on ? HIGH : LOW);
    s_on = on;
}

} // namespace

void haptics_play(ui::Haptic pattern, uint32_t now_ms)
{
    switch (pattern) {
    case ui::Haptic::Tick: s_steps = kTick; s_count = 1; break;
    case ui::Haptic::Double: s_steps = kDouble; s_count = 3; break;
    default: return;
    }
    s_index = 0;
    s_step_start = now_ms;
    motor(true);
}

void haptics_update(uint32_t now_ms)
{
    if (!s_steps) return;
    while (s_index < s_count && now_ms - s_step_start >= s_steps[s_index]) {
        s_step_start += s_steps[s_index];
        ++s_index;
    }
    if (s_index >= s_count) {
        motor(false);
        s_steps = nullptr;
        return;
    }
    motor((s_index % 2) == 0);
}
