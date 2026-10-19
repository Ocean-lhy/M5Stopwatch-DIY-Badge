#pragma once

#include <stdint.h>

#include "badge_config.h"

enum PortalChange : uint32_t {
    PORTAL_CHANGED_CONFIG = 1u << 0,
    PORTAL_PUBLISHED = 1u << 1,
    PORTAL_RESET = 1u << 2,
    PORTAL_TIME = 1u << 3,
    // Bits 8..15 carry BadgeLayerFlags of uploaded layers.
    PORTAL_LAYER_SHIFT = 8,
};

// Brings up netif/Wi-Fi/HTTP once; the radio itself stays off until portal_start().
bool portal_init(BadgeConfig *config);
// Starts the SoftAP with a fresh random password (setup mode only).
bool portal_start();
void portal_stop();
bool portal_running();

// The HTTP task edits the shared config under this lock.
void portal_lock();
void portal_unlock();
uint32_t portal_take_changes();

int portal_stations();
// esp_timer milliseconds of the last HTTP request or station join.
uint32_t portal_last_activity_ms();
void portal_set_telemetry(int battery_percent, bool charging, float runtime_h);

const char *portal_ssid();
const char *portal_password();
const char *portal_wifi_qr();
const char *portal_url();
