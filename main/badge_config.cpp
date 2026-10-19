#include "badge_config.h"

#include "badge_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "nvs.h"

static constexpr uint32_t kVersion = 2;
static constexpr char kNamespace[] = "badge";

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
    // Never leave a truncated UTF-8 sequence at the end.
    size_t len = strlen(dst);
    size_t i = len;
    while (i > 0 && ((unsigned char)dst[i - 1] & 0xC0) == 0x80) --i;
    if (i > 0) {
        unsigned char lead = (unsigned char)dst[i - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (len - (i - 1) < need) dst[i - 1] = 0;
    }
}

static uint16_t rgb565_from_hex(const char *hex, uint16_t fallback)
{
    if (!hex) return fallback;
    if (hex[0] == '#') ++hex;
    if (strlen(hex) != 6) return fallback;
    char *end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (!end || *end) return fallback;
    uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void hex_from_rgb565(uint16_t c, char out[8])
{
    uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    snprintf(out, 8, "#%02X%02X%02X", (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

// Same presets as THEMES in web/compose.js: bg, ink, muted, accent.
static constexpr const char *kThemeColors[][4] = {
    {"#000000", "#F2F6FA", "#8A9BB0", "#3BA7FF"},
    {"#032447", "#FFFFFF", "#A0C4E6", "#7FD0FF"},
    {"#F4F1EA", "#1A1A1A", "#6E6A62", "#E0542E"},
};

void badge_config_defaults(BadgeConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = kVersion;
    copy_field(cfg->name, sizeof(cfg->name), kProfile_name);
    copy_field(cfg->title, sizeof(cfg->title), kProfile_title);
    copy_field(cfg->company, sizeof(cfg->company), kProfile_company);
    copy_field(cfg->intro, sizeof(cfg->intro), kProfile_intro);
    copy_field(cfg->seeking, sizeof(cfg->seeking), kProfile_seeking);
    copy_field(cfg->booth, sizeof(cfg->booth), kProfile_booth);
    copy_field(cfg->tags, sizeof(cfg->tags), kProfile_tags);
    copy_field(cfg->email, sizeof(cfg->email), kProfile_email);
    copy_field(cfg->phone, sizeof(cfg->phone), kProfile_phone);
    cfg->theme = (uint8_t)kProfileTheme;
    const char *const *colors = kThemeColors[kProfileTheme];
    cfg->bg = rgb565_from_hex(colors[0], 0);
    cfg->ink = rgb565_from_hex(colors[1], 0xFFFF);
    cfg->muted = rgb565_from_hex(colors[2], 0x8410);
    cfg->accent = rgb565_from_hex(colors[3], 0x3D3F);
    cfg->brightness = 200;
    cfg->ambient_brightness = 40;
    cfg->ambient_after_s = 45;
    cfg->scan_timeout_s = 30;
    cfg->touch_enabled = true;
    cfg->about_enabled = true;
    cfg->tz_offset_min = (int16_t)kProfileTzOffsetMin;
    cfg->qr_count = (uint8_t)kProfileQrCount;
    for (int i = 0; i < kProfileQrCount && i < kBadgeQrSlots; ++i) {
        copy_field(cfg->qr[i].label, sizeof(cfg->qr[i].label), kProfileQr[i][0]);
        copy_field(cfg->qr[i].text, sizeof(cfg->qr[i].text), kProfileQr[i][1]);
    }
}

bool badge_config_load(BadgeConfig *cfg)
{
    badge_config_defaults(cfg);
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    BadgeConfig loaded;
    size_t len = sizeof(loaded);
    esp_err_t err = nvs_get_blob(handle, "config", &loaded, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(loaded) || loaded.version != kVersion) return false;
    *cfg = loaded;
    if (cfg->qr_count > kBadgeQrSlots) cfg->qr_count = kBadgeQrSlots;
    for (auto &slot : cfg->qr) {
        slot.label[sizeof(slot.label) - 1] = 0;
        slot.text[sizeof(slot.text) - 1] = 0;
    }
    return cfg->published;
}

bool badge_config_save(const BadgeConfig *cfg)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, "config", cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static void read_string(const cJSON *root, const char *key, char *dst, size_t len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item)) copy_field(dst, len, item->valuestring);
}

static bool read_number(const cJSON *root, const char *key, double lo, double hi, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) return false;
    double v = item->valuedouble;
    *out = v < lo ? lo : v > hi ? hi : v;
    return true;
}

static void read_bool(const cJSON *root, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) *out = cJSON_IsTrue(item);
}

bool badge_config_apply_json(const char *json, BadgeConfig *cfg)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    read_string(root, "name", cfg->name, sizeof(cfg->name));
    read_string(root, "title", cfg->title, sizeof(cfg->title));
    read_string(root, "company", cfg->company, sizeof(cfg->company));
    read_string(root, "intro", cfg->intro, sizeof(cfg->intro));
    read_string(root, "seeking", cfg->seeking, sizeof(cfg->seeking));
    read_string(root, "booth", cfg->booth, sizeof(cfg->booth));
    read_string(root, "tags", cfg->tags, sizeof(cfg->tags));
    read_string(root, "email", cfg->email, sizeof(cfg->email));
    read_string(root, "phone", cfg->phone, sizeof(cfg->phone));

    double v = 0;
    if (read_number(root, "theme", 0, 2, &v)) cfg->theme = (uint8_t)v;
    const cJSON *colors = cJSON_GetObjectItemCaseSensitive(root, "colors");
    if (cJSON_IsObject(colors)) {
        auto color = [&](const char *key, uint16_t *dst) {
            const cJSON *c = cJSON_GetObjectItemCaseSensitive(colors, key);
            if (cJSON_IsString(c)) *dst = rgb565_from_hex(c->valuestring, *dst);
        };
        color("bg", &cfg->bg);
        color("ink", &cfg->ink);
        color("muted", &cfg->muted);
        color("accent", &cfg->accent);
    }
    if (read_number(root, "brightness", 40, 255, &v)) cfg->brightness = (uint8_t)v;
    if (read_number(root, "ambientBrightness", 0, 120, &v)) cfg->ambient_brightness = (uint8_t)v;
    if (read_number(root, "ambientAfter", 0, 3600, &v)) cfg->ambient_after_s = (uint16_t)v;
    if (cfg->ambient_after_s > 0 && cfg->ambient_after_s < 10) cfg->ambient_after_s = 10;
    if (read_number(root, "scanTimeout", 10, 300, &v)) cfg->scan_timeout_s = (uint16_t)v;
    if (read_number(root, "tz", -720, 840, &v)) cfg->tz_offset_min = (int16_t)v;
    read_bool(root, "touch", &cfg->touch_enabled);
    read_bool(root, "about", &cfg->about_enabled);

    const cJSON *qr = cJSON_GetObjectItemCaseSensitive(root, "qr");
    if (cJSON_IsArray(qr)) {
        int count = 0;
        const cJSON *slot = nullptr;
        cJSON_ArrayForEach(slot, qr)
        {
            if (count >= kBadgeQrSlots) break;
            const cJSON *text = cJSON_GetObjectItemCaseSensitive(slot, "text");
            if (!cJSON_IsString(text) || !text->valuestring[0]) continue;
            if (strlen(text->valuestring) >= sizeof(cfg->qr[0].text)) continue;
            copy_field(cfg->qr[count].text, sizeof(cfg->qr[count].text), text->valuestring);
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(slot, "label");
            copy_field(cfg->qr[count].label, sizeof(cfg->qr[count].label),
                       cJSON_IsString(label) ? label->valuestring : "");
            ++count;
        }
        cfg->qr_count = (uint8_t)count;
    }
    bool publish = false;
    read_bool(root, "publish", &publish);
    if (publish) cfg->published = true;
    cJSON_Delete(root);
    return true;
}

cJSON *badge_config_to_cjson(const BadgeConfig *cfg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "published", cfg->published);
    cJSON_AddStringToObject(root, "name", cfg->name);
    cJSON_AddStringToObject(root, "title", cfg->title);
    cJSON_AddStringToObject(root, "company", cfg->company);
    cJSON_AddStringToObject(root, "intro", cfg->intro);
    cJSON_AddStringToObject(root, "seeking", cfg->seeking);
    cJSON_AddStringToObject(root, "booth", cfg->booth);
    cJSON_AddStringToObject(root, "tags", cfg->tags);
    cJSON_AddStringToObject(root, "email", cfg->email);
    cJSON_AddStringToObject(root, "phone", cfg->phone);
    cJSON_AddNumberToObject(root, "theme", cfg->theme);
    cJSON *colors = cJSON_AddObjectToObject(root, "colors");
    char hex[8];
    hex_from_rgb565(cfg->bg, hex);
    cJSON_AddStringToObject(colors, "bg", hex);
    hex_from_rgb565(cfg->ink, hex);
    cJSON_AddStringToObject(colors, "ink", hex);
    hex_from_rgb565(cfg->muted, hex);
    cJSON_AddStringToObject(colors, "muted", hex);
    hex_from_rgb565(cfg->accent, hex);
    cJSON_AddStringToObject(colors, "accent", hex);
    cJSON_AddNumberToObject(root, "brightness", cfg->brightness);
    cJSON_AddNumberToObject(root, "ambientBrightness", cfg->ambient_brightness);
    cJSON_AddNumberToObject(root, "ambientAfter", cfg->ambient_after_s);
    cJSON_AddNumberToObject(root, "scanTimeout", cfg->scan_timeout_s);
    cJSON_AddNumberToObject(root, "tz", cfg->tz_offset_min);
    cJSON_AddBoolToObject(root, "touch", cfg->touch_enabled);
    cJSON_AddBoolToObject(root, "about", cfg->about_enabled);
    cJSON_AddNumberToObject(root, "layers", cfg->layers);
    cJSON *qr = cJSON_AddArrayToObject(root, "qr");
    for (int i = 0; i < cfg->qr_count && i < kBadgeQrSlots; ++i) {
        cJSON *slot = cJSON_CreateObject();
        cJSON_AddStringToObject(slot, "label", cfg->qr[i].label);
        cJSON_AddStringToObject(slot, "text", cfg->qr[i].text);
        cJSON_AddItemToArray(qr, slot);
    }
    return root;
}

static uint32_t s_scans = 0;
static bool s_scans_loaded = false;

uint32_t badge_scan_count()
{
    if (!s_scans_loaded) {
        nvs_handle_t handle = 0;
        if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
            nvs_get_u32(handle, "scans", &s_scans);
            nvs_close(handle);
        }
        s_scans_loaded = true;
    }
    return s_scans;
}

void badge_scan_count_increment()
{
    badge_scan_count();
    ++s_scans;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u32(handle, "scans", s_scans);
    nvs_commit(handle);
    nvs_close(handle);
}
