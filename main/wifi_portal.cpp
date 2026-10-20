#include "wifi_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <initializer_list>

#include "cJSON.h"
#include "dns_server.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "layers.h"
#include "nvs.h"
#include "power.h"
#include "rtc_time.h"
#include "web_assets.h"

static const char *TAG = "badge_portal";
static constexpr char kIp[] = "192.168.4.1";
static constexpr char kPortalUri[] = "http://192.168.4.1/";
static constexpr char kVersion[] = "1.0.0";
static constexpr size_t kMaxJsonBody = 4096;
static constexpr size_t kMaxAsset = 256 * 1024;

static BadgeConfig *g_config = nullptr;
static SemaphoreHandle_t g_lock = nullptr;
static httpd_handle_t g_server = nullptr;
static esp_netif_t *g_netif = nullptr;
static bool g_running = false;
static char g_ssid[33] = "M5Badge";
static char g_password[16] = "";
static char g_wifi_qr[96] = "";
static std::atomic<uint32_t> g_changes{0};
static std::atomic<int> g_stations{0};
static std::atomic<uint32_t> g_activity{0};
static std::atomic<int> g_battery{-1};
static std::atomic<bool> g_charging{false};
static std::atomic<float> g_runtime{-1};

extern "C" const uint8_t _binary_avatar_jpg_start[];
extern "C" const uint8_t _binary_avatar_jpg_end[];

static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }
static void touch_activity() { g_activity = now_ms(); }
static void post_change(uint32_t bits) { g_changes.fetch_or(bits); }

void portal_lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
void portal_unlock() { xSemaphoreGive(g_lock); }
uint32_t portal_take_changes() { return g_changes.exchange(0); }
int portal_stations() { return g_stations; }
uint32_t portal_last_activity_ms() { return g_activity; }
bool portal_running() { return g_running; }
const char *portal_ssid() { return g_ssid; }
const char *portal_password() { return g_password; }
const char *portal_wifi_qr() { return g_wifi_qr; }
const char *portal_url() { return kIp; }

void portal_set_telemetry(int battery_percent, bool charging, float runtime_h)
{
    g_battery = battery_percent;
    g_charging = charging;
    g_runtime = runtime_h;
}

// ---------------------------------------------------------------- HTTP helpers

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":false,\"message\":\"%s\"}", message);
    return send_json(req, status, body);
}

static char *read_body(httpd_req_t *req, size_t limit)
{
    if (req->content_len <= 0 || (size_t)req->content_len > limit) return nullptr;
    char *body = static_cast<char *>(malloc(req->content_len + 1));
    if (!body) return nullptr;
    int received = 0;
    while (received < (int)req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            return nullptr;
        }
        received += n;
    }
    body[received] = 0;
    return body;
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    const WebAsset *asset = static_cast<const WebAsset *>(req->user_ctx);
    httpd_resp_set_type(req, asset->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, reinterpret_cast<const char *>(asset->data), asset->size);
}

// Set once the editor has been served this session. From then on connectivity probes
// get the "online" answer, so the OS sign-in window can be closed without leaving the
// network and the editor can be opened in a real browser (which can pick photos).
static std::atomic<bool> g_editor_seen{false};

struct ProbeAnswer {
    const char *path;
    const char *status;
    const char *type;
    const char *body;
};

static constexpr ProbeAnswer kProbeAnswers[] = {
    {"/hotspot-detect.html", "200 OK", "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"},
    {"/library/test/success.html", "200 OK", "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"},
    {"/generate_204", "204 No Content", "text/plain", ""},
    {"/gen_204", "204 No Content", "text/plain", ""},
    {"/ncsi.txt", "200 OK", "text/plain", "Microsoft NCSI"},
    {"/connecttest.txt", "200 OK", "text/plain", "Microsoft Connect Test"},
    {"/success.txt", "200 OK", "text/plain", "success\n"},
    {"/canonical.html", "200 OK", "text/html", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>"},
};

static esp_err_t redirect_handler(httpd_req_t *req);

static esp_err_t probe_handler(httpd_req_t *req)
{
    if (!g_editor_seen) return redirect_handler(req);
    touch_activity();
    const auto *answer = static_cast<const ProbeAnswer *>(req->user_ctx);
    httpd_resp_set_status(req, answer->status);
    httpd_resp_set_type(req, answer->type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, answer->body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    touch_activity();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", kPortalUri);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t)
{
    if (strncmp(req->uri, "/api/", 5) == 0) return send_error(req, "404 Not Found", "not found");
    // WeChat keeps probing /mmtls while the phone is on the AP. Redirecting each
    // one makes it open yet another connection to the editor and crowds out the
    // system captive-portal check.
    if (strncmp(req->uri, "/mmtls/", 7) == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, "", 0);
    }
    return redirect_handler(req);
}

// ---------------------------------------------------------------- API

static esp_err_t state_handler(httpd_req_t *req)
{
    g_editor_seen = true;
    touch_activity();
    portal_lock();
    cJSON *root = badge_config_to_cjson(g_config);
    portal_unlock();
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddNumberToObject(device, "battery", g_battery);
    cJSON_AddBoolToObject(device, "charging", g_charging);
    cJSON_AddNumberToObject(device, "runtime", g_runtime.load());
    cJSON_AddStringToObject(device, "ssid", g_ssid);
    cJSON_AddStringToObject(device, "ip", kIp);
    cJSON_AddStringToObject(device, "version", kVersion);
    cJSON_AddBoolToObject(device, "timeValid", rtc_time_valid());
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t err = send_json(req, "200 OK", text ? text : "{}");
    free(text);
    return err;
}

static esp_err_t power_handler(httpd_req_t *req)
{
    touch_activity();
    const PowerState st = power_state();
    PowerSample *samples = static_cast<PowerSample *>(calloc(kPowerHistoryCap, sizeof(PowerSample)));
    PowerUsedPoint *learned = static_cast<PowerUsedPoint *>(calloc(kPowerCurveCap, sizeof(PowerUsedPoint)));
    PowerVoltPoint factory[16];
    const int n_s = samples ? power_copy_samples(samples, kPowerHistoryCap) : 0;
    const int n_l = learned ? power_copy_learned(learned, kPowerCurveCap) : 0;
    const int n_f = power_copy_factory(factory, 16);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(samples);
        free(learned);
        return send_error(req, "500 Internal Server Error", "oom");
    }
    cJSON_AddNumberToObject(root, "mv", st.mv);
    cJSON_AddNumberToObject(root, "ma", st.load_ma);
    cJSON_AddNumberToObject(root, "percent", st.percent);
    cJSON_AddNumberToObject(root, "runtime", st.runtime_h);
    cJSON_AddBoolToObject(root, "charging", st.charging);
    cJSON_AddNumberToObject(root, "used_mah", st.used_mah);
    cJSON_AddNumberToObject(root, "capacity_mah", st.capacity_mah);
    cJSON_AddNumberToObject(root, "motor_mah", st.motor_mah);
    cJSON_AddBoolToObject(root, "learned", st.learned);
    cJSON_AddBoolToObject(root, "from_full", st.from_full);

    cJSON *arr = cJSON_AddArrayToObject(root, "samples");
    for (int i = 0; i < n_s; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "t", samples[i].t);
        cJSON_AddNumberToObject(item, "mv", samples[i].mv);
        cJSON_AddNumberToObject(item, "ma", samples[i].ma_x10 / 10.0);
        cJSON_AddNumberToObject(item, "h", samples[i].remain_x10 / 10.0);
        cJSON_AddBoolToObject(item, "charging", samples[i].charging);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON *curve = cJSON_AddArrayToObject(root, "learned_curve");
    for (int i = 0; i < n_l; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "mv", learned[i].mv);
        cJSON_AddNumberToObject(item, "used", learned[i].used_x10 / 10.0);
        cJSON_AddItemToArray(curve, item);
    }
    cJSON *fac = cJSON_AddArrayToObject(root, "factory_curve");
    for (int i = 0; i < n_f; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "mv", factory[i].mv);
        cJSON_AddNumberToObject(item, "pct", factory[i].pct);
        cJSON_AddItemToArray(fac, item);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(samples);
    free(learned);
    esp_err_t err = send_json(req, "200 OK", text ? text : "{}");
    free(text);
    return err;
}

static esp_err_t power_reset_handler(httpd_req_t *req)
{
    touch_activity();
    power_clear_calibration();
    return send_json(req, "200 OK", "{\"ok\":true}");
}

static void apply_time(const cJSON *root)
{
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
    if (cJSON_IsNumber(tz)) rtc_time_set_tz((int16_t)tz->valuedouble);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (cJSON_IsNumber(t)) {
        rtc_time_sync((int64_t)t->valuedouble);
        post_change(PORTAL_TIME);
    }
}

static esp_err_t config_handler(httpd_req_t *req)
{
    touch_activity();
    char *body = read_body(req, kMaxJsonBody);
    if (!body) return send_error(req, "400 Bad Request", "invalid body");
    cJSON *root = cJSON_Parse(body);
    if (root) apply_time(root);
    bool publish = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "publish"));
    cJSON_Delete(root);

    portal_lock();
    bool ok = badge_config_apply_json(body, g_config) && badge_config_save(g_config);
    portal_unlock();
    free(body);
    if (!ok) return send_error(req, "500 Internal Server Error", "save failed");
    post_change(PORTAL_CHANGED_CONFIG | (publish ? (uint32_t)PORTAL_PUBLISHED : 0u));
    return send_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t time_handler(httpd_req_t *req)
{
    touch_activity();
    char *body = read_body(req, 256);
    if (!body) return send_error(req, "400 Bad Request", "invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_error(req, "400 Bad Request", "invalid json");
    apply_time(root);
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
    if (cJSON_IsNumber(tz)) {
        portal_lock();
        if (g_config->tz_offset_min != (int16_t)tz->valuedouble) {
            g_config->tz_offset_min = (int16_t)tz->valuedouble;
            badge_config_save(g_config);
        }
        portal_unlock();
    }
    cJSON_Delete(root);
    return send_json(req, "200 OK", "{\"ok\":true}");
}

struct AssetTarget {
    const char *name;
    const char *path;
    uint8_t layer;
};

static constexpr AssetTarget kAssetTargets[] = {
    {"card", kLayerCardPath, BADGE_LAYER_CARD},
    {"about", kLayerAboutPath, BADGE_LAYER_ABOUT},
    {"name", kLayerNamePath, BADGE_LAYER_NAME},
    {"avatar", kAvatarPath, 0},
};

static esp_err_t asset_upload_handler(httpd_req_t *req)
{
    touch_activity();
    char name[16] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Asset-Name", name, sizeof(name)) != ESP_OK)
        return send_error(req, "400 Bad Request", "missing X-Asset-Name");
    const AssetTarget *target = nullptr;
    for (const auto &t : kAssetTargets)
        if (strcmp(name, t.name) == 0) target = &t;
    if (!target) return send_error(req, "400 Bad Request", "unknown asset");
    if (req->content_len < 4 || req->content_len > kMaxAsset)
        return send_error(req, "413 Payload Too Large", "asset size not accepted");

    char tmp_path[40];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target->path);
    FILE *file = fopen(tmp_path, "wb");
    if (!file) return send_error(req, "500 Internal Server Error", "storage write failed");
    static uint8_t buffer[2048];
    size_t remaining = req->content_len;
    bool first = true;
    bool ok = true;
    while (remaining > 0) {
        int want = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        int n = httpd_req_recv(req, reinterpret_cast<char *>(buffer), want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        if (first) {
            first = false;
            if (n < 2 || buffer[0] != 0xFF || buffer[1] != 0xD8) {
                ok = false;
                break;
            }
        }
        if (fwrite(buffer, 1, n, file) != (size_t)n) {
            ok = false;
            break;
        }
        remaining -= n;
    }
    fclose(file);
    if (!ok) {
        unlink(tmp_path);
        return send_error(req, "400 Bad Request", "upload interrupted or not a JPEG");
    }
    // SPIFFS rename does not replace an existing file.
    unlink(target->path);
    if (rename(tmp_path, target->path) != 0) {
        unlink(tmp_path);
        return send_error(req, "500 Internal Server Error", "storage rename failed");
    }
    if (target->layer) {
        portal_lock();
        g_config->layers |= target->layer;
        badge_config_save(g_config);
        portal_unlock();
        post_change((uint32_t)target->layer << PORTAL_LAYER_SHIFT);
    }
    return send_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t avatar_handler(httpd_req_t *req)
{
    touch_activity();
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    FILE *file = fopen(kAvatarPath, "rb");
    if (!file) {
        return httpd_resp_send(req, reinterpret_cast<const char *>(_binary_avatar_jpg_start),
                               _binary_avatar_jpg_end - _binary_avatar_jpg_start);
    }
    static char chunk[2048];
    size_t n = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(file);
            return ESP_FAIL;
        }
    }
    fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    touch_activity();
    for (const auto &t : kAssetTargets) unlink(t.path);
    portal_lock();
    badge_config_defaults(g_config);
    bool ok = badge_config_save(g_config);
    portal_unlock();
    post_change(PORTAL_RESET | PORTAL_CHANGED_CONFIG |
                ((uint32_t)(BADGE_LAYER_CARD | BADGE_LAYER_ABOUT | BADGE_LAYER_NAME) << PORTAL_LAYER_SHIFT));
    return ok ? send_json(req, "200 OK", "{\"ok\":true}") : send_error(req, "500 Internal Server Error", "reset failed");
}

// ---------------------------------------------------------------- Wi-Fi

static void wifi_event_handler(void *, esp_event_base_t, int32_t id, void *)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        ++g_stations;
        touch_activity();
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (g_stations > 0) --g_stations;
        touch_activity();
    }
}

static void append_escaped(char *dst, size_t len, const char *src)
{
    size_t pos = strlen(dst);
    for (; *src && pos + 2 < len; ++src) {
        if (strchr("\\;,:\"", *src)) dst[pos++] = '\\';
        dst[pos++] = *src;
    }
    dst[pos] = 0;
}

static void new_password()
{
    // No 0/o/1/l/i so the password can be typed from the screen if needed.
    static constexpr char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    for (int i = 0; i < 8; ++i) g_password[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    g_password[8] = 0;
    snprintf(g_wifi_qr, sizeof(g_wifi_qr), "WIFI:T:WPA;S:");
    append_escaped(g_wifi_qr, sizeof(g_wifi_qr), g_ssid);
    strncat(g_wifi_qr, ";P:", sizeof(g_wifi_qr) - strlen(g_wifi_qr) - 1);
    append_escaped(g_wifi_qr, sizeof(g_wifi_qr), g_password);
    strncat(g_wifi_qr, ";;", sizeof(g_wifi_qr) - strlen(g_wifi_qr) - 1);
}

// The editor shows the uploaded avatar when there is one. When a firmware build ships
// a different default (profile/avatar.jpg), drop the old upload so the new one shows.
static void forget_avatar_if_default_changed()
{
    const uint32_t crc = esp_rom_crc32_le(0, _binary_avatar_jpg_start, _binary_avatar_jpg_end - _binary_avatar_jpg_start);
    nvs_handle_t handle = 0;
    if (nvs_open("badge", NVS_READWRITE, &handle) != ESP_OK) return;
    uint32_t stored = 0;
    if (nvs_get_u32(handle, "avatar_crc", &stored) != ESP_OK || stored != crc) {
        unlink(kAvatarPath);
        nvs_set_u32(handle, "avatar_crc", crc);
        nvs_commit(handle);
        ESP_LOGI(TAG, "built-in avatar changed, using it as the editor default");
    }
    nvs_close(handle);
}

static void init_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    // Leftovers from an interrupted upload.
    for (const auto &t : kAssetTargets) {
        char tmp_path[40];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", t.path);
        unlink(tmp_path);
    }
    forget_avatar_if_default_changed();
}

static void register_get(const char *uri, esp_err_t (*handler)(httpd_req_t *), void *ctx = nullptr)
{
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = HTTP_GET;
    u.handler = handler;
    u.user_ctx = ctx;
    httpd_register_uri_handler(g_server, &u);
}

static void register_post(const char *uri, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = HTTP_POST;
    u.handler = handler;
    httpd_register_uri_handler(g_server, &u);
}

static bool start_http()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = WEB_ASSET_COUNT + 20;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    // httpd reserves 3 sockets besides max_open_sockets, and DNS takes one more.
    // With the old CONFIG_LWIP_MAX_SOCKETS=10 that left nothing for the phone's
    // parallel captive probes, so accept() failed with ENFILE (23) and the
    // system sign-in window never appeared.
    cfg.max_open_sockets = 7;
    cfg.backlog_conn = 4;
    cfg.recv_wait_timeout = 2;
    cfg.send_wait_timeout = 2;
    if (httpd_start(&g_server, &cfg) != ESP_OK) return false;
    for (size_t i = 0; i < WEB_ASSET_COUNT; ++i) register_get(WEB_ASSETS[i].path, asset_handler, (void *)&WEB_ASSETS[i]);
    register_get("/api/state", state_handler);
    register_get("/api/power", power_handler);
    register_post("/api/power/reset", power_reset_handler);
    register_get("/avatar.jpg", avatar_handler);
    register_post("/api/config", config_handler);
    register_post("/api/time", time_handler);
    register_post("/api/assets", asset_upload_handler);
    register_post("/api/reset", reset_handler);
    for (const auto &answer : kProbeAnswers) register_get(answer.path, probe_handler, (void *)&answer);
    for (const char *path : {"/redirect", "/fwlink"}) register_get(path, redirect_handler);
    httpd_register_err_handler(g_server, HTTPD_404_NOT_FOUND, not_found_handler);
    return true;
}

bool portal_init(BadgeConfig *config)
{
    g_config = config;
    g_lock = xSemaphoreCreateMutex();
    init_spiffs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    g_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));

    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(g_ssid, sizeof(g_ssid), "M5Badge-%02X%02X", mac[4], mac[5]);

    esp_netif_dhcps_stop(g_netif);
    esp_netif_ip_info_t ip{};
    ip.ip.addr = esp_ip4addr_aton(kIp);
    ip.gw.addr = ip.ip.addr;
    ip.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ESP_ERROR_CHECK(esp_netif_set_ip_info(g_netif, &ip));
    // RFC 8910: lets Android 11+ / iOS 14+ open the editor right after joining.
    esp_err_t err = esp_netif_dhcps_option(g_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                                           (void *)kPortalUri, strlen(kPortalUri));
    if (err != ESP_OK) ESP_LOGW(TAG, "captive portal DHCP option unsupported: %s", esp_err_to_name(err));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(g_netif));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    dns_server_start();
    if (!start_http()) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return false;
    }
    return true;
}

bool portal_start()
{
    if (g_running) return true;
    new_password();
    wifi_config_t ap{};
    const size_t ssid_len = strnlen(g_ssid, sizeof(ap.ap.ssid));
    memcpy(ap.ap.ssid, g_ssid, ssid_len);
    memcpy(ap.ap.password, g_password, strlen(g_password));
    ap.ap.ssid_len = ssid_len;
    ap.ap.channel = 6;
    ap.ap.max_connection = 3;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "AP start failed");
        return false;
    }
    g_stations = 0;
    g_editor_seen = false;
    touch_activity();
    g_running = true;
    ESP_LOGI(TAG, "AP started: %s / %s", g_ssid, g_password);
    return true;
}

void portal_stop()
{
    if (!g_running) return;
    esp_wifi_stop();
    g_running = false;
    g_stations = 0;
    ESP_LOGI(TAG, "AP stopped");
}
