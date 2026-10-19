#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

constexpr int kBadgeQrSlots = 3;

enum BadgeLayerFlags : uint8_t {
    BADGE_LAYER_CARD = 1u << 0,
    BADGE_LAYER_ABOUT = 1u << 1,
    BADGE_LAYER_NAME = 1u << 2,
};

struct BadgeQrSlot {
    char label[32];
    char text[256];
};

// Versioned model shared by the web editor and the firmware. Rendered layers
// and the source avatar live in SPIFFS; everything else fits in one NVS blob.
// Text fields are kept so the editor can re-open and re-render the layers.
struct BadgeConfig {
    uint32_t version;
    bool published;
    char name[48];
    char title[48];
    char company[48];
    char intro[200];
    char seeking[120];
    char booth[24];
    char tags[96];  // comma separated
    char email[64];
    char phone[24];
    uint8_t theme;  // 0 ocean, 1 brand, 2 paper
    uint16_t bg;    // RGB565 theme colors, sent by the editor
    uint16_t ink;
    uint16_t muted;
    uint16_t accent;
    uint8_t brightness;
    uint8_t ambient_brightness;
    uint16_t ambient_after_s; // 0 = never
    uint16_t scan_timeout_s;
    bool touch_enabled;
    bool about_enabled;
    int16_t tz_offset_min;
    uint8_t layers; // BadgeLayerFlags present in SPIFFS
    uint8_t qr_count;
    BadgeQrSlot qr[kBadgeQrSlots];
};

void badge_config_defaults(BadgeConfig *cfg);
bool badge_config_load(BadgeConfig *cfg);
bool badge_config_save(const BadgeConfig *cfg);
// Applies a JSON object from the editor; unknown keys are ignored and values are clamped.
bool badge_config_apply_json(const char *json, BadgeConfig *cfg);
struct cJSON;
cJSON *badge_config_to_cjson(const BadgeConfig *cfg);

uint32_t badge_scan_count();
void badge_scan_count_increment();
