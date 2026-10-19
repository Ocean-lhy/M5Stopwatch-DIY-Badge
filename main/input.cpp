#include "input.h"

#include "driver/gpio.h"

namespace {

// KEYA (yellow) is G2 and KEYB (blue) is G1, both active low.
constexpr gpio_num_t kKeyA = GPIO_NUM_2;
constexpr gpio_num_t kKeyB = GPIO_NUM_1;
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kLongPressMs = 2000;

struct Key {
    gpio_num_t pin;
    bool stable;
    bool raw;
    uint32_t raw_since;
    uint32_t down_at;
    bool long_fired;
};

Key s_keys[2] = {{kKeyA, false, false, 0, 0, false}, {kKeyB, false, false, 0, 0, false}};

} // namespace

bool input_init()
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << kKeyA) | (1ULL << kKeyB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    return true;
}

int input_poll(uint32_t now, ui::Input *out, int capacity)
{
    int n = 0;
    auto emit = [&](ui::Input e) {
        if (n < capacity) out[n++] = e;
    };

    for (int i = 0; i < 2; ++i) {
        Key &k = s_keys[i];
        bool raw = gpio_get_level(k.pin) == 0;
        if (raw != k.raw) {
            k.raw = raw;
            k.raw_since = now;
        }
        if (raw != k.stable && now - k.raw_since >= kDebounceMs) {
            k.stable = raw;
            if (raw) {
                k.down_at = now;
                k.long_fired = false;
            } else if (!k.long_fired) {
                emit(i == 0 ? ui::Input::KeyA : ui::Input::KeyB);
            }
        }
        // Only the blue key has a long-press action; fire while held for feedback.
        if (i == 1 && k.stable && !k.long_fired && now - k.down_at >= kLongPressMs) {
            k.long_fired = true;
            emit(ui::Input::KeyBLong);
        }
    }
    return n;
}
