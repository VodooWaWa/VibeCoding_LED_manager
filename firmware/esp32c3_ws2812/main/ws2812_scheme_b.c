/*
 * WS2812 8-LED AI Status Indicator — Scheme B (full-featured)
 * ESP32-C3 SuperMini
 *
 * Features:
 *   8 animations (rainbow, liquid breath, yellow scan, red/green breath,
 *                triple flash, red-blue flip, off)
 *   WiFi credentials from NVS with default fallback
 *   TCP server on port 8080 for state commands
 *   BLE GATT server (Bluedroid) as WiFi fallback
 *   HTTP server with WebUI, help page, brightness, status, WiFi config, lang
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "mdns.h"
#include "led_strip.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_phy_init.h"
#include "esp_bt.h"
#include "esp_bt_main.h"


#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"

#define TAG "ws2812"

// --- BLE Constants ---

#define BLE_APP_ID            0x55
#define GATTS_CHAR_VAL_LEN_MAX 32
#define CHAR_DECLARATION_SIZE  (sizeof(uint8_t))

// --- LED Config ---
#define LED_PIN     8
#define LED_COUNT   8

// --- WiFi ---
#define AP_SSID_PREFIX "3DAi_LED_"
#define AP_IP        "192.168.4.1"
#define TCP_PORT     8080

static led_strip_handle_t strip;
static uint8_t brightness = 128;
// Per-LED activity timestamps for individual idle timeout (microseconds)
static int64_t last_activity_us[LED_COUNT] = {0};

// --- Static IP config ---
static char static_ip[16] = "";
static char static_gw[16] = "";
static char static_mask[16] = "";
static bool use_static_ip = false;

// --- Idle auto-off: configurable timeout (default 120s) ---
#define DEFAULT_IDLE_TIMEOUT_MS 120000
static uint32_t idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;

// --- WiFi credentials from NVS ---
static char wifi_ssid[33] = "";
static char wifi_pass[65] = "";
static bool has_saved_wifi = false;  // true if NVS had real credentials
static bool is_ap_mode = false;
static uint8_t g_lang = 0;
static char g_hostname[32] = "3dai-led";
static char device_suffix[9] = "00000000";

// --- Helper: brightness scaling ---
static inline uint8_t scale(uint8_t c) {
    return (uint8_t)(((uint16_t)c * brightness) >> 8);
}

// --- HSV -> RGB ---
typedef struct { uint8_t r, g, b; } rgb_t;

static inline rgb_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r, g, b;
    if (h < 60)      { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    rgb_t out = {
        .r = (uint8_t)((r + m) * 255),
        .g = (uint8_t)((g + m) * 255),
        .b = (uint8_t)((b + m) * 255),
    };
    return out;
}

// LED direction: when true, LED #1 (index 0) maps to rightmost physical LED
static bool led_reverse = true;
static inline int phys_led_idx(int n) { return led_reverse ? (LED_COUNT - 1 - n) : n; }

static inline void set_led_hsv(int i, float h, float s, float v) {
    rgb_t c = hsv_to_rgb(fmodf(h, 360.0f), s, v);
    led_strip_set_pixel(strip, phys_led_idx(i), scale(c.r), scale(c.g), scale(c.b));
}

// --- Animation State ---
typedef enum {
    STATE_THINKING,
    STATE_CODING,
    STATE_BUSY,
    STATE_WAITING,
    STATE_SUCCESS,
    STATE_ERROR,
    STATE_ALARM,
    STATE_OFF,
    STATE_COUNT
} anim_state_t;

static const char *state_names[] = {
    "thinking", "coding", "busy", "waiting",
    "success", "error", "alarm", "off"
};
static const char *state_colors[] = {
    "#ff6b6b", "#bb86fc", "#ffaa00", "#e04040",
    "#4caf50", "#ff4444", "#ff0000", "#666"
};

// Per-LED state: each of the 8 LEDs runs an independent animation.
// Backward-compatible single-strip mode uses only LED 0 (index 0).
static anim_state_t led_states[LED_COUNT]   = {STATE_OFF};
static anim_state_t pending_states[LED_COUNT] = {STATE_OFF};
static anim_state_t target_states[LED_COUNT]  = {STATE_OFF};
static int64_t      pending_since_us_arr[LED_COUNT] = {0};

static bool multi_project_mode = false;
static bool test_mode_active = false;    // true when test is running
static bool test_all_leds = false;      // true when test sets all 8 LEDs (not single-LED)
static char project_names[LED_COUNT][32];   // project name → LED mapping
static char platform_names[LED_COUNT][16];  // platform per LED (claude/codex/opencode/mimo)
static char alloc_keys[LED_COUNT][48];      // "platform:project" composite key for allocation

// Test-mode display states — never modify led_states[] (real state) during tests.
// Keeps the home page /status reporting accurate while test animation runs on LEDs.
static anim_state_t test_led_states[LED_COUNT] = {STATE_OFF};

#define STATE_DEBOUNCE_US 50000   // 50ms: coalesce rapid state changes

// ============================================================
// LED allocator — odd-even priority assignment
// ============================================================
// 1 project  → LED 1 (index 0)
// 2 projects → LEDs 1,5 (indexes 0,4)
// 3 projects → LEDs 1,3,5 (indexes 0,2,4)
// 4 projects → LEDs 1,3,5,7 (indexes 0,2,4,6)
// 5 projects → LEDs 1,2,3,5,7 (indexes 0,1,2,4,6)
// 6 projects → LEDs 1,2,3,4,5,7 (indexes 0,1,2,3,4,6)
// 7 projects → LEDs 1,2,3,4,5,6,7 (indexes 0,1,2,3,4,5,6)
// 8 projects → LEDs 1,2,3,4,5,6,7,8 (all)
static const uint8_t alloc_order[8] = {0, 4, 2, 6, 1, 3, 5, 7};

// Auto-assign LED to project+platform.  When platform is provided each
// (platform, project) pair gets its own LED — same project from different
// platforms won't collide.  Returns existing LED if already mapped, or
// allocates first free LED via alloc_order.  Returns -1 if all 8 busy.
static int alloc_led_for_project(const char *name, const char *platform) {
    if (!name || !name[0]) return -1;
    char key[48];
    if (platform && platform[0])
        snprintf(key, sizeof(key), "%s:%s", platform, name);
    else
        snprintf(key, sizeof(key), "%s", name);
    // 1. Check if this composite key already has an LED
    for (int n = 0; n < LED_COUNT; n++) {
        if (alloc_keys[n][0] && strcmp(alloc_keys[n], key) == 0)
            return n;
    }
    // 2. Allocate first free LED in priority order
    for (int k = 0; k < LED_COUNT; k++) {
        uint8_t idx = alloc_order[k];
        if (alloc_keys[idx][0] == 0) {
            strncpy(alloc_keys[idx], key, sizeof(alloc_keys[idx]) - 1);
            alloc_keys[idx][sizeof(alloc_keys[idx]) - 1] = 0;
            strncpy(project_names[idx], name, sizeof(project_names[idx]) - 1);
            project_names[idx][sizeof(project_names[idx]) - 1] = 0;
            if (platform && platform[0]) {
                strncpy(platform_names[idx], platform, sizeof(platform_names[idx]) - 1);
                platform_names[idx][sizeof(platform_names[idx]) - 1] = 0;
            }
            ESP_LOGI(TAG, "Project '%s' (platform %s) assigned LED %d",
                     name, platform && platform[0] ? platform : "-", idx + 1);
            return idx;
        }
    }
    return -1;  // all busy
}

// Extract " platform:name" suffix from project_name string.
// Truncates *pp at the keyword, writes platform to out[].
// Also handles no-project-name edge case where string starts with "platform:".
static void parse_platform(char **pp, char *out, size_t out_sz) {
    if (!*pp || !**pp) return;
    char *kw = strstr(*pp, " platform:");
    if (!kw && strncmp(*pp, "platform:", 9) == 0) {
        kw = *pp;                    // no project name, string IS "platform:xxx"
    }
    if (!kw) return;
    *kw = '\0';                      // truncate project name (or set it to "")
    const char *name = kw + (kw == *pp ? 9 : 10);  // skip "platform:" or " platform:"
    strncpy(out, name, out_sz - 1);
    out[out_sz - 1] = 0;
}

// ============================================================
// Animation: all functions now operate on a single LED at index *n*
// instead of the whole strip.
// ============================================================
// --- Per-LED animation helpers ---

static void anim_led_liquid_breath(int n, uint32_t t_ms) {
    float phase = (float)(t_ms % 3000) / 3000.0f;
    float breath = 0.5f + 0.5f * sinf(phase * 2 * M_PI);
    float sat = 0.6f + 0.4f * breath;
    float val = 0.15f + breath * 0.35f;
    float hue = 170.0f + n * 5.0f;  // cyan→purple spread
    set_led_hsv(n, hue, sat, val);
}

static void anim_led_rainbow(int n, uint32_t t_ms) {
    float offset = (float)(t_ms % 3600) / 10.0f;
    float hue = fmodf(offset + n * 45.0f, 360.0f);
    set_led_hsv(n, hue, 0.8f, 0.65f);
}

// Classic full-strip yellow ping-pong scan (busy, normal mode).
// Dark-bright-dark gradient sweeps 3 LEDs back and forth across all 8.
static void anim_yellow_scan(uint32_t t_ms) {
    float cycle = (float)(t_ms % 1000) / 1000.0f;
    float scan_pos;
    if (cycle < 0.5f) scan_pos = cycle * 2.0f;           // 0→1  left to right
    else              scan_pos = 2.0f - cycle * 2.0f;    // 1→0  right to left

    for (int i = 0; i < LED_COUNT; i++) {
        float led_pos = (float)i / (LED_COUNT - 1);
        float dist = fabsf(led_pos - scan_pos);
        float bri = fmaxf(0, 1.0f - dist * 4.0f);
        bri = bri * bri;  // quadratic falloff for smooth gradient
        uint8_t val = scale((uint8_t)(255 * bri));
        led_strip_set_pixel(strip, phys_led_idx(i), val, (uint8_t)(val * 0.65f), 0);
    }
}

// Multi-project mode: per-LED independent yellow pulse, staggered by LED index
static void anim_led_busy_pulse(int n, uint32_t t_ms) {
    uint32_t local_t = (t_ms + n * 120) % 800;
    float bri;
    if (local_t < 80) {
        bri = (float)local_t / 80.0f;                     // attack 0→1  (80ms)
    } else if (local_t < 160) {
        bri = 1.0f - (float)(local_t - 80) / 80.0f;      // decay  1→0  (80ms)
    } else {
        bri = 0.0f;                                        // off     (640ms)
    }
    uint8_t v = scale((uint8_t)(255 * bri));
    led_strip_set_pixel(strip, phys_led_idx(n), v, (uint8_t)(v * 0.65f), 0);
}

static void anim_led_red_breath(int n, uint32_t t_ms) {
    float phase = (float)(t_ms % 3000) / 3000.0f;
    float breath = 0.1f + 0.45f * (sinf(phase * 2 * M_PI) * 0.5f + 0.5f);
    uint8_t r = scale((uint8_t)(255 * breath));
    led_strip_set_pixel(strip, phys_led_idx(n), r, 0, 0);
}

static void anim_led_green_breath(int n, uint32_t t_ms) {
    float phase = (float)(t_ms % 3000) / 3000.0f;
    float breath = 0.1f + 0.45f * (sinf(phase * 2 * M_PI) * 0.5f + 0.5f);
    uint8_t g = scale((uint8_t)(255 * breath));
    led_strip_set_pixel(strip, phys_led_idx(n), 0, g, 0);
}

static void anim_led_red_blue_flip(int n, uint32_t t_ms) {
    bool red_phase = ((t_ms / 180) % 2) == 0;
    if (red_phase) led_strip_set_pixel(strip, phys_led_idx(n), scale(255), 0, 0);
    else           led_strip_set_pixel(strip, phys_led_idx(n), 0, 0, scale(220));
}

static void anim_led_triple_flash(int n, uint32_t t_ms) {
    uint32_t p = t_ms % 750;
    uint8_t bri = 0;
    if (p < 50)            bri = 255;
    else if (p < 100)      bri = 0;
    else if (p < 130)      bri = 200;
    else if (p < 180)      bri = 0;
    else if (p < 230)      bri = 255;
    else if (p < 260)      bri = 0;
    else if (p < 290)      bri = 200;
    else if (p < 340)      bri = 0;
    else if (p < 390)      bri = 255;
    else if (p < 420)      bri = 0;
    else if (p < 450)      bri = 200;
    else                   bri = 0;
    uint8_t r = scale(bri);
    uint8_t g = scale((uint8_t)(bri * 0.12f));
    led_strip_set_pixel(strip, phys_led_idx(n), r, g, 0);
}

// --- Render dispatchers (two sets) ---

// Single-strip mode: original animations that use full-strip context
static void render_led_single(int n, anim_state_t st, uint32_t t_ms) {
    switch (st) {
    case STATE_THINKING: anim_led_rainbow(n, t_ms);       break;
    case STATE_CODING:   anim_led_liquid_breath(n, t_ms); break;
    case STATE_BUSY:     anim_yellow_scan(t_ms);          break;
    case STATE_WAITING:  anim_led_red_breath(n, t_ms);    break;
    case STATE_SUCCESS:  anim_led_green_breath(n, t_ms);  break;
    case STATE_ERROR:    anim_led_triple_flash(n, t_ms);  break;
    case STATE_ALARM:    anim_led_red_blue_flip(n, t_ms); break;
    case STATE_OFF:      led_strip_set_pixel(strip, phys_led_idx(n), 0, 0, 0); break;
    default:             led_strip_set_pixel(strip, phys_led_idx(n), 0, 0, 0); break;
    }
}

// Multi-project / individual mode: per-LED-friendly animations
static void render_led_multi(int n, anim_state_t st, uint32_t t_ms) {
    switch (st) {
    case STATE_THINKING: anim_led_rainbow(n, t_ms);       break;
    case STATE_CODING:   anim_led_liquid_breath(n, t_ms); break;
    case STATE_BUSY:
        if (test_mode_active)
            test_all_leds ? anim_yellow_scan(t_ms) : anim_led_busy_pulse(n, t_ms);
        else if (multi_project_mode) anim_led_busy_pulse(n, t_ms);
        else anim_yellow_scan(t_ms);
        break;
    case STATE_WAITING:  anim_led_red_breath(n, t_ms);    break;
    case STATE_SUCCESS:  anim_led_green_breath(n, t_ms);  break;
    case STATE_ERROR:    anim_led_triple_flash(n, t_ms);  break;
    case STATE_ALARM:    anim_led_red_blue_flip(n, t_ms); break;
    case STATE_OFF:      led_strip_set_pixel(strip, phys_led_idx(n), 0, 0, 0); break;
    default:             led_strip_set_pixel(strip, phys_led_idx(n), 0, 0, 0); break;
    }
}

static void update_animation(void) {
    uint32_t t_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Per-LED idle timeout: skip when test mode active (JS timer controls lifecycle)
    if (idle_timeout_ms > 0 && !test_mode_active) {
        int64_t now = esp_timer_get_time();
        for (int n = 0; n < LED_COUNT; n++) {
            if (led_states[n] != STATE_OFF &&
                (now - last_activity_us[n]) / 1000 > (int64_t)idle_timeout_ms) {
                led_states[n] = STATE_OFF;
                target_states[n] = STATE_OFF;
                pending_states[n] = STATE_OFF;
                if (project_names[n][0]) {
                    ESP_LOGI(TAG, "LED %d project '%s' released (idle timeout %lu s)",
                             n + 1, project_names[n], (unsigned long)(idle_timeout_ms / 1000));
                    project_names[n][0] = 0;
                    platform_names[n][0] = 0;
                    alloc_keys[n][0] = 0;
                }
            }
        }
    }

    // Check if any non-zero LED has an active state (test mode / single-LED control)
    bool any_individual_active = false;
    if (!multi_project_mode) {
        for (int n = 1; n < LED_COUNT; n++) {
            if (led_states[n] != STATE_OFF) { any_individual_active = true; break; }
        }
    }

    // Auto-exit test mode when all test LEDs are OFF
    if (test_mode_active) {
        bool all_test_off = true;
        for (int n = 0; n < LED_COUNT; n++) {
            if (test_led_states[n] != STATE_OFF) { all_test_off = false; break; }
        }
        if (all_test_off) {
            test_mode_active = false;
            test_all_leds = false;
        }
    }

    if (test_mode_active) {
        // Test mode: render from test_led_states, real led_states untouched
        for (int n = 0; n < LED_COUNT; n++)
            render_led_multi(n, test_led_states[n], t_ms);
    } else if (multi_project_mode || any_individual_active) {
        // Multi-project / individual: per-LED animations
        for (int n = 0; n < LED_COUNT; n++)
            render_led_multi(n, led_states[n], t_ms);
    } else {
        // Single-strip: LED 0 drives all, full-strip animations
        for (int n = 0; n < LED_COUNT; n++)
            render_led_single(n, led_states[0], t_ms);
    }
    led_strip_refresh(strip);
}

// --- State change handler ---
// Commands arrive at any time (TCP/BLE/HTTP). Rather than apply immediately
// (which can cause flicker from rapid off→thinking→off flips), we record the
// desired state and let the animation loop apply it after a 50 ms quiet period.
static void set_state_idx(int led, anim_state_t state) {
    test_mode_active = false;  // normal state change exits test mode
    if (idle_timeout_ms > 0)
        last_activity_us[led] = esp_timer_get_time();
    pending_states[led] = state;
    pending_since_us_arr[led] = esp_timer_get_time();
}

static void set_state(anim_state_t state) {
    set_state_idx(0, state);  // backward compat — LED 0
}

static void apply_pending_state(void) {
    for (int led = 0; led < LED_COUNT; led++) {
        if (pending_since_us_arr[led] == 0) continue;
        int64_t now = esp_timer_get_time();
        if (now - pending_since_us_arr[led] < STATE_DEBOUNCE_US) continue;
        target_states[led] = pending_states[led];
        pending_since_us_arr[led] = 0;
        if (target_states[led] != led_states[led]) {
            led_states[led] = target_states[led];
            ESP_LOGI(TAG, "LED %d state: %s", led + 1, state_names[led_states[led]]);
        }
        // Explicit OFF always releases the project slot (even if already OFF)
        if (target_states[led] == STATE_OFF && project_names[led][0]) {
            ESP_LOGI(TAG, "LED %d project '%s' released (off)", led + 1, project_names[led]);
            project_names[led][0] = 0;
            platform_names[led][0] = 0;
            alloc_keys[led][0] = 0;
        }
    }
}

// ===================================================================
// TCP Server (port 8080) — FreeRTOS task
// ===================================================================

static void tcp_server_task(void *arg) {
    int listen_sock;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    while (1) {
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock < 0) {
            ESP_LOGE(TAG, "TCP: socket failed, retry in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "TCP: bind failed, retry in 5s");
            close(listen_sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (listen(listen_sock, 3) < 0) {
            ESP_LOGE(TAG, "TCP: listen failed, retry in 5s");
            close(listen_sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        break;
    }

    ESP_LOGI(TAG, "TCP server listening on port %d", TCP_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Set socket timeout to prevent blocking forever
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        char client_ip[16];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "TCP client connected: %s", client_ip);

        // Send greeting with device identifier for reliable discovery
        char greeting[64];
        int glen = snprintf(greeting, sizeof(greeting), "3dai-led %s ready\n", g_hostname);
        send(client_sock, greeting, glen, 0);

        // Read one line at a time, process commands
        char line_buf[128];
        int line_pos = 0;

        while (1) {
            char c;
            int ret = recv(client_sock, &c, 1, 0);
            if (ret <= 0) break;  // disconnect or error

            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';

                    // Trim trailing \r or spaces
                    while (line_pos > 0 && (line_buf[line_pos-1] == '\r' || line_buf[line_pos-1] == ' '))
                        line_buf[--line_pos] = '\0';

                    if (strcmp(line_buf, "ping") == 0) {
                        send(client_sock, "pong\n", 5, 0);
                    } else if (strcmp(line_buf, "status") == 0) {
                        char resp[128];
                        int off = 0;
                        for (int n = 0; n < LED_COUNT; n++) {
                            off += snprintf(resp + off, sizeof(resp) - off,
                                            "%d:%s ", n, state_names[led_states[n]]);
                        }
                        resp[off - 1] = '\n'; resp[off] = '\0';
                        send(client_sock, resp, off, 0);
                    } else {
                        // Parse: "state project_name platform:name" or bare "state"
                        const char *state_str = line_buf;
                        char *project_name = NULL;
                        char platform[16] = {0};
                        char *space = strchr(line_buf, ' ');
                        if (space) {
                            *space = '\0';
                            state_str = line_buf;
                            project_name = space + 1;
                            parse_platform(&project_name, platform, sizeof(platform));
                        }
                        bool matched = false;
                        for (int i = 0; i < STATE_COUNT; i++) {
                            if (strcmp(state_str, state_names[i]) == 0) {
                                int led = 0;
                                if (multi_project_mode && project_name && project_name[0]) {
                                    led = alloc_led_for_project(project_name, platform);
                                    if (led < 0) {
                                        send(client_sock, "full 8 LEDs busy\n", 17, 0);
                                        matched = true; break;
                                    }  // all busy, immediate response so client doesn't timeout
                                }
                                set_state_idx(led, (anim_state_t)i);
                                if (platform[0]) strncpy(platform_names[led], platform, sizeof(platform_names[led]) - 1);
                                char resp[64];
                                int l = snprintf(resp, sizeof(resp), "ok %d:%s\n", led, state_names[i]);
                                send(client_sock, resp, l, 0);
                                matched = true;
                                break;
                            }
                        }
                        if (!matched) {
                            char resp[64];
                            int l = snprintf(resp, sizeof(resp), "error unknown_state %s\n", line_buf);
                            send(client_sock, resp, l, 0);
                        }
                    }
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }

        close(client_sock);
        ESP_LOGI(TAG, "TCP client disconnected: %s", client_ip);
    }
}

// ===================================================================
// ===================================================================
// BLE GATT Server (Bluedroid) — fallback when WiFi is not available
// ===================================================================
// Uses attribute table approach from official gatt_server_service_table example
// 128-bit UUIDs match transport_ble.py for cross-platform compatibility
// Just Works pairing with Secure Connections + Bonding

static char ble_device_name[32] = "3DAi_LED_00000000";
static bool ble_enabled = true;
static bool ble_discoverable = false;
static bool ble_initialized = false;
static uint16_t ble_conn_id = 0;
static TimerHandle_t ble_disc_timer = NULL;

// Forward declarations
static void save_ble_discoverable_to_nvs(bool en);
static void save_idle_timeout_to_nvs(uint32_t ms);
static void ble_start_advertising(void);
static void ble_runtime_enable(void);
static void ble_runtime_disable(void);

// ---- BLE discovery auto-off timer ----
// ble_discoverable = show device name in BLE scans. 120s auto-off is
// correct — once paired, you don't need to be visible in scan lists.
// This does NOT stop BLE advertising — state commands still work.
static void ble_disc_timer_cb(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "BLE discoverable timeout (advertising continues)");
    ble_discoverable = false;
    save_ble_discoverable_to_nvs(false);
}

// ---- 128-bit UUIDs matching transport_ble.py ----
// Service: 0000FF00-0000-1000-8000-00805F9B34FB
// Write:   0000FF01-0000-1000-8000-00805F9B34FB
// Notify:  0000FF02-0000-1000-8000-00805F9B34FB
// BLE wire format: LSB first

static const uint8_t svc_uuid_128[ESP_UUID_LEN_128] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
};
static const uint8_t chr_write_uuid_128[ESP_UUID_LEN_128] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00,
};
static const uint8_t chr_notify_uuid_128[ESP_UUID_LEN_128] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00,
};

// Standard BLE UUIDs
static const uint16_t primary_service_uuid       = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t cccd_init_value[2] = {0x00, 0x00};
static const uint8_t char_init_value[2] = {0x00, 0x00};

// Attribute table indices
enum {
    IDX_SVC,
    IDX_CHAR_DECL_A,
    IDX_CHAR_VAL_A,
    IDX_CHAR_CFG_A,
    IDX_CHAR_DECL_B,
    IDX_CHAR_VAL_B,
    HRS_IDX_NB,
};

static uint16_t led_handle_table[HRS_IDX_NB];

// GATT database — attribute table
static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    // Service Declaration (128-bit UUID)
    [IDX_SVC] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
      ESP_UUID_LEN_128, ESP_UUID_LEN_128, (uint8_t *)svc_uuid_128}},

    // Characteristic A Declaration (WRITE)
    [IDX_CHAR_DECL_A] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},

    // Characteristic A Value (128-bit UUID, WRITE permission)
    [IDX_CHAR_VAL_A] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)chr_write_uuid_128, ESP_GATT_PERM_WRITE,
      GATTS_CHAR_VAL_LEN_MAX, sizeof(char_init_value), (uint8_t *)char_init_value}},

    // Characteristic A CCCD (for client to enable notify/indicate)
    [IDX_CHAR_CFG_A] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
      ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(cccd_init_value), (uint8_t *)cccd_init_value}},

    // Characteristic B Declaration (NOTIFY)
    [IDX_CHAR_DECL_B] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_notify}},

    // Characteristic B Value (128-bit UUID, READ + NOTIFY)
    [IDX_CHAR_VAL_B] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)chr_notify_uuid_128,
      ESP_GATT_PERM_READ, GATTS_CHAR_VAL_LEN_MAX, sizeof(char_init_value), (uint8_t *)char_init_value}},
};

// Advertising config flags
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)
static uint8_t adv_config_done = 0;

// Advertising data — device name included for iOS/macOS compatibility.
// 128-bit UUID moved to scan_rsp (31B adv limit: flags(3) + name(19) = 22B).
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp    = false,
    .include_name    = true,
    .include_txpower = false,
    .service_uuid_len = 0,
    .p_service_uuid   = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Scan response data — 128-bit service UUID for app filtering
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp     = true,
    .include_name     = false,
    .include_txpower  = true,
    .service_uuid_len = ESP_UUID_LEN_128,
    .p_service_uuid   = (uint8_t *)svc_uuid_128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Advertising parameters
static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = ESP_BLE_GAP_ADV_ITVL_MS(100),
    .adv_int_max       = ESP_BLE_GAP_ADV_ITVL_MS(150),
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ---- GAP event handler ----
static void ble_gap_event_handler(esp_gap_ble_cb_event_t event,
                                   esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "BLE: adv start failed, status=%d",
                     param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "BLE: advertising started as '%s'", ble_device_name);
            if (ble_disc_timer) {
                xTimerReset(ble_disc_timer, pdMS_TO_TICKS(100));
            }
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "BLE: adv stop failed");
        } else {
            ESP_LOGI(TAG, "BLE: advertising stopped");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "BLE: conn params update status=%d min_int=%d max_int=%d latency=%d timeout=%d",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    // ---- BLE Security events (bonding/pairing) ----
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!param->ble_security.auth_cmpl.success) {
            ESP_LOGW(TAG, "BLE: pairing failed, reason=0x%x",
                     param->ble_security.auth_cmpl.fail_reason);
        } else {
            ESP_LOGI(TAG, "BLE: paired successfully, auth_mode=%d",
                     param->ble_security.auth_cmpl.auth_mode);
        }
        break;
    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGI(TAG, "BLE: key exchanged, type=%d",
                 param->ble_security.ble_key.key_type);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGI(TAG, "BLE: passkey notify %06" PRIu32,
                 param->ble_security.key_notif.passkey);
        break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        ESP_LOGI(TAG, "BLE: passkey request");
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        ESP_LOGI(TAG, "BLE: numeric comparison request, passkey=%" PRIu32,
                 param->ble_security.key_notif.passkey);
        esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI(TAG, "BLE: bond removed, status=%d",
                 param->remove_bond_dev_cmpl.status);
        break;
    default:
        break;
    }
}

// ---- GATT profile event handler ----
static void ble_gatts_profile_handler(esp_gatts_cb_event_t event,
                                       esp_gatt_if_t gatts_if,
                                       esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        // Set both bits BEFORE async calls to prevent premature advertising
        adv_config_done = (ADV_CONFIG_FLAG | SCAN_RSP_CONFIG_FLAG);
        esp_err_t ret = esp_ble_gap_set_device_name(ble_device_name);
        if (ret) {
            ESP_LOGE(TAG, "BLE: set device name failed: %d", ret);
        }
        ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret) {
            ESP_LOGE(TAG, "BLE: config adv data failed: %d", ret);
            adv_config_done &= (~ADV_CONFIG_FLAG);
        }
        ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (ret) {
            ESP_LOGE(TAG, "BLE: config scan rsp data failed: %d", ret);
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        }
        ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if,
                                             HRS_IDX_NB, 0);
        if (ret) {
            ESP_LOGE(TAG, "BLE: create attr table failed: %d", ret);
        }
        break;
    }
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "BLE: GATT read, handle=%d", param->read.handle);
        break;
    case ESP_GATTS_WRITE_EVT:
        // Accept writes without encryption — characteristic permissions
        // (ESP_GATT_PERM_WRITE, no _ENCRYPTED suffix) don't mandate it.
        if (!param->write.is_prep) {
            int len = param->write.len;
            if (len > 31) len = 31;
            char buf[32] = {0};
            memcpy(buf, param->write.value, len);
            for (int i = 0; i < len; i++) {
                if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = 0; break; }
            }
            ESP_LOGI(TAG, "BLE: write handle=%d len=%d value='%s'",
                     param->write.handle, param->write.len, buf);
            // Parse "state project_name platform:name" or bare "state"
            const char *state_str = buf;
            char *project_name = NULL;
            char platform[16] = {0};
            char *spc = strchr(buf, ' ');
            if (spc) { *spc = '\0'; state_str = buf; project_name = spc + 1;
                       parse_platform(&project_name, platform, sizeof(platform)); }
            for (int i = 0; i < STATE_COUNT; i++) {
                if (strcmp(state_str, state_names[i]) == 0) {
                    int led = 0;
                    if (multi_project_mode && project_name && project_name[0]) {
                        led = alloc_led_for_project(project_name, platform);
                        if (led < 0) {
                            ESP_LOGW(TAG, "BLE: all 8 LEDs busy, discarding '%s'", buf);
                            break;
                        }
                    }
                    set_state_idx(led, (anim_state_t)i);
                    if (platform[0]) strncpy(platform_names[led], platform, sizeof(platform_names[led]) - 1);
                    ESP_LOGI(TAG, "BLE state -> %s (LED %d)", state_str, led + 1);
                    break;
                }
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                             param->write.trans_id,
                                             ESP_GATT_OK, NULL);
            }
        }
        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(TAG, "BLE: exec write");
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "BLE: MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_CONF_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "BLE: service started, handle=%d status=%d",
                 param->start.service_handle, param->start.status);
        break;
    case ESP_GATTS_CONNECT_EVT:
        ble_conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "BLE: connected, conn_id=%d", ble_conn_id);
        ESP_LOG_BUFFER_HEX(TAG, param->connect.remote_bda, 6);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "BLE: disconnected, reason=0x%x", param->disconnect.reason);
        ble_conn_id = 0;
        if (ble_enabled) {
            ble_start_advertising();
        }
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "BLE: create attr table failed, err=0x%x",
                     param->add_attr_tab.status);
        } else if (param->add_attr_tab.num_handle != HRS_IDX_NB) {
            ESP_LOGE(TAG, "BLE: attr table num_handle mismatch (%d != %d)",
                     param->add_attr_tab.num_handle, HRS_IDX_NB);
        } else {
            ESP_LOGI(TAG, "BLE: attr table created, %d handles",
                     param->add_attr_tab.num_handle);
            memcpy(led_handle_table, param->add_attr_tab.handles,
                   sizeof(led_handle_table));
            esp_ble_gatts_start_service(led_handle_table[IDX_SVC]);
        }
        break;
    }
    case ESP_GATTS_STOP_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    case ESP_GATTS_UNREG_EVT:
    case ESP_GATTS_DELETE_EVT:
    default:
        break;
    }
}

// ---- Top-level GATT event handler ----
static void ble_gatts_event_handler(esp_gatts_cb_event_t event,
                                     esp_gatt_if_t gatts_if,
                                     esp_ble_gatts_cb_param_t *param) {
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "BLE: GATT registered, app_id=%d, gatts_if=%d",
                     param->reg.app_id, gatts_if);
        } else {
            ESP_LOGE(TAG, "BLE: GATT register failed, app_id=%d status=%d",
                     param->reg.app_id, param->reg.status);
            return;
        }
    }
    ble_gatts_profile_handler(event, gatts_if, param);
}

// ---- Start BLE advertising ----
static void ble_start_advertising(void) {
    if (!ble_enabled) return;
    // Set both bits BEFORE async calls — prevents race where
    // a callback fires and sees adv_config_done==0, starting
    // advertising before scan_rsp (name + service UUID) is ready.
    adv_config_done = (ADV_CONFIG_FLAG | SCAN_RSP_CONFIG_FLAG);
    esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret) {
        ESP_LOGE(TAG, "BLE: config adv data failed: %d", ret);
        adv_config_done &= (~ADV_CONFIG_FLAG);
        return;
    }
    ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
    if (ret) {
        ESP_LOGE(TAG, "BLE: config scan rsp data failed: %d", ret);
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
    }
}

// ---- BLE initialize and start ----
static void ble_init_and_start(void) {
    if (ble_initialized) {
        ESP_LOGI(TAG, "BLE already initialized");
        return;
    }
    // Use device_suffix for consistent naming with AP SSID
    snprintf(ble_device_name, sizeof(ble_device_name), "3DAi_LED_%s", device_suffix);
    ESP_LOGI(TAG, "BLE: initializing Bluedroid...");
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BLE: controller init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BLE: controller enable failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "BLE: bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "BLE: bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }
    // Configure SMP — Just Works pairing with Secure Connections + Bonding.
    // Bonding persists the encryption keys so the OS remembers this device.
    // Python transport connects directly to the bonded address (no scanning).
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    // Bonding keys — allows OS to remember device after Just Works pairing.
    // Pairing is optional: GATT writes work without encryption.
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    // Register callbacks
    ret = esp_ble_gatts_register_callback(ble_gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "BLE: gatts register callback failed: %d", ret);
        return;
    }
    ret = esp_ble_gap_register_callback(ble_gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "BLE: gap register callback failed: %d", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(BLE_APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "BLE: app register failed: %d", ret);
        return;
    }
    ret = esp_ble_gatt_set_local_mtu(500);
    if (ret) {
        ESP_LOGE(TAG, "BLE: set local MTU failed: %d", ret);
    }
    if (ble_disc_timer == NULL) {
        ble_disc_timer = xTimerCreate("ble_disc", pdMS_TO_TICKS(120000),
                                       pdFALSE, NULL, ble_disc_timer_cb);
    }
    ble_initialized = true;
    ESP_LOGI(TAG, "BLE: Bluedroid started as %s (discoverable=%d)",
             ble_device_name, ble_discoverable);
}

// ===================================================================
// WiFi — load credentials from NVS, connect
// ===================================================================

static void load_nvs_config(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("led", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using defaults");
        return;
    }

    size_t len;

    // Load WiFi SSID
    len = sizeof(wifi_ssid);
    err = nvs_get_str(nvs, "ssid", wifi_ssid, &len);
    if (err == ESP_OK && wifi_ssid[0] != '\0') {
        // Load WiFi password
        len = sizeof(wifi_pass);
        err = nvs_get_str(nvs, "pwd", wifi_pass, &len);
        if (err == ESP_OK && wifi_pass[0] != '\0') {
            has_saved_wifi = true;
        }
    }

    // Load brightness
    uint8_t b;
    err = nvs_get_u8(nvs, "bri", &b);
    if (err == ESP_OK && b >= 1) {
        brightness = b;
    }

    // Load language
    err = nvs_get_u8(nvs, "lang", &g_lang);
    if (err != ESP_OK) {
        g_lang = 0;  // default zh
    }

    // Load BLE enabled (default: true)
    uint8_t ble_val;
    err = nvs_get_u8(nvs, "ble", &ble_val);
    if (err == ESP_OK) {
        ble_enabled = (ble_val != 0);
    }

    // Load BLE discoverable (default: true)
    err = nvs_get_u8(nvs, "bledisc", &ble_val);
    if (err == ESP_OK) {
        ble_discoverable = (ble_val != 0);
    }

    // Load idle timeout (default: 120s, 0 = never)
    uint32_t idle_val;
    err = nvs_get_u32(nvs, "idle_to", &idle_val);
    if (err == ESP_OK && idle_val <= 3600000) {
        idle_timeout_ms = idle_val;
    }

    // Load static IP config
    len = sizeof(static_ip);
    if (nvs_get_str(nvs, "sip", static_ip, &len) == ESP_OK && static_ip[0]) {
        len = sizeof(static_gw);
        nvs_get_str(nvs, "sgw", static_gw, &len);
        len = sizeof(static_mask);
        nvs_get_str(nvs, "smk", static_mask, &len);
        uint8_t v;
        if (nvs_get_u8(nvs, "sip_en", &v) == ESP_OK) {
            use_static_ip = (v != 0);
        }
    }

    // Load LED direction
    uint8_t rev_val;
    err = nvs_get_u8(nvs, "ledrev", &rev_val);
    if (err == ESP_OK) {
        led_reverse = (rev_val != 0);
    }

    // Load multi-project mode
    uint8_t mp_val;
    err = nvs_get_u8(nvs, "multi", &mp_val);
    if (err == ESP_OK) {
        multi_project_mode = (mp_val != 0);
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "NVS loaded: WiFi=%s, brightness=%d, lang=%s, BLE=%d(disc=%d), idle=%lus, multi=%d",
             has_saved_wifi ? wifi_ssid : "(not configured)", brightness, g_lang ? "en" : "zh",
             ble_enabled, ble_discoverable, (unsigned long)(idle_timeout_ms / 1000),
             multi_project_mode);
}

static void save_led_reverse_to_nvs(bool rev) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "ledrev", rev ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_multi_project_to_nvs(bool en) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "multi", en ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_wifi_to_nvs(const char *ssid, const char *pwd) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "ssid", ssid);
        nvs_set_str(nvs, "pwd", pwd);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_brightness_to_nvs(uint8_t b) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "bri", b);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_lang_to_nvs(uint8_t lang) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "lang", lang);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_ble_enabled_to_nvs(bool en) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "ble", en ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_ble_discoverable_to_nvs(bool en) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "bledisc", en ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_idle_timeout_to_nvs(uint32_t ms) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, "idle_to", ms);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void save_static_ip_to_nvs(const char *ip, const char *gw, const char *mask, bool en) {
    nvs_handle_t nvs;
    if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "sip", ip);
        nvs_set_str(nvs, "sgw", gw);
        nvs_set_str(nvs, "smk", mask);
        nvs_set_u8(nvs, "sip_en", en ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static int wifi_retry_count = 0;

static void wifi_disconnect_handler(void *arg, esp_event_base_t base,
                                     int32_t id, void *data) {
    wifi_retry_count++;
    ESP_LOGW(TAG, "WiFi disconnected, retry %d/5", wifi_retry_count);
    if (wifi_retry_count <= 5) {
        esp_wifi_connect();
    } else {
        ESP_LOGE(TAG, "WiFi retry exhausted, restarting to AP mode...");
        nvs_handle_t nvs;
        if (nvs_open("led", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_key(nvs, "ssid");
            nvs_erase_key(nvs, "pwd");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

static bool static_ip_applied = false;

static void wifi_got_ip_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    wifi_retry_count = 0;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    // Apply static IP if configured (DHCP → static switch, one-shot)
    if (use_static_ip && !static_ip_applied && static_ip[0]) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_dhcpc_stop(netif);
            esp_netif_ip_info_t ip_info;
            ip_info.ip.addr = ipaddr_addr(static_ip);
            ip_info.gw.addr = ipaddr_addr(static_gw);
            ip_info.netmask.addr = ipaddr_addr(static_mask);
            esp_netif_set_ip_info(netif, &ip_info);
            static_ip_applied = true;
            ESP_LOGI(TAG, "Static IP applied: %s/%s/%s", static_ip, static_gw, static_mask);
        }
    }
}

static bool is_wifi_connected(void) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return false;
    esp_netif_ip_info_t ip;
    return esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void get_ip_str(char *buf, size_t len) {
    if (is_ap_mode) {
        snprintf(buf, len, "%s", AP_IP);
        return;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
            return;
        }
    }
    snprintf(buf, len, "0.0.0.0");
}

// ===================================================================
// HTTP Server — buffer + single send (avoids non-blocking chunk EAGAIN)
// ===================================================================

static char *g_resp_buf = NULL;
static size_t g_resp_len = 0;
static size_t g_resp_cap = 0;

static void resp_init(void) {
    if (!g_resp_buf) { g_resp_cap = 32768; g_resp_buf = malloc(g_resp_cap); }
    g_resp_len = 0;
    if (g_resp_buf) g_resp_buf[0] = 0;
}

static void resp_append(const char *str) {
    if (!g_resp_buf) return;
    size_t slen = strlen(str);
    if (g_resp_len + slen < g_resp_cap) {
        memcpy(g_resp_buf + g_resp_len, str, slen);
        g_resp_len += slen;
        g_resp_buf[g_resp_len] = 0;
    }
}

static void resp_flush(httpd_req_t *req) {
    if (g_resp_buf && g_resp_len > 0)
        httpd_resp_send(req, g_resp_buf, g_resp_len);
}

#define RESP_SEND(str) resp_append(str)

// Forward declarations for helpers used across HTTP handlers
static void json_escape_ssid(char *dst, const char *src, size_t dst_len);
static void html_escape(char *dst, const char *src, size_t dst_len);
static void url_decode(char *dst, const char *src, size_t dst_len);

// --- /status — JSON ---
static esp_err_t handle_status(httpd_req_t *req) {
    static char json[768];
    char ip[16] = {0};
    get_ip_str(ip, sizeof(ip));
    if (ip[0] == '\0' && is_ap_mode) {
        snprintf(ip, sizeof(ip), "%s", AP_IP);
    }

    // Build state list
    char states_json[256] = "";
    for (int i = 0; i < STATE_COUNT; i++) {
        char part[32];
        snprintf(part, sizeof(part), "%s\"%s\"", (i > 0 ? "," : ""), state_names[i]);
        strncat(states_json, part, sizeof(states_json) - strlen(states_json) - 1);
    }
    // Per-LED state array for dashboard
    char led_states_json[128] = "";
    for (int n = 0; n < LED_COUNT; n++) {
        char led_part[16];
        snprintf(led_part, sizeof(led_part), "%s\"%s\"", (n > 0 ? "," : ""), state_names[led_states[n]]);
        strncat(led_states_json, led_part, sizeof(led_states_json) - strlen(led_states_json) - 1);
    }

    char esc_ssid[66] = {0};
    json_escape_ssid(esc_ssid, wifi_ssid, sizeof(esc_ssid));
    int bond_count = (ble_enabled && ble_initialized) ? esp_ble_get_bond_device_num() : 0;
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"brightness\":%d,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"ble_enabled\":%s,\"ble_discoverable\":%s,\"idle_timeout\":%lu,"
        "\"multi_led\":%s,\"ble_bond_count\":%d,\"led_states\":[%s],\"states\":[%s]}",
        state_names[led_states[0]], brightness, esc_ssid, ip,
        ble_enabled ? "true" : "false", ble_discoverable ? "true" : "false",
        (unsigned long)(idle_timeout_ms / 1000),
        multi_project_mode ? "true" : "false", bond_count,
        led_states_json, states_json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// --- /set?s=state&ble=1&bledisc=1 ---
static esp_err_t handle_set(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char state[16] = {0};
        if (httpd_query_key_value(buf, "s", state, sizeof(state)) == ESP_OK) {
            char led_str[4] = {0};
            int led = 0;
            if (httpd_query_key_value(buf, "led", led_str, sizeof(led_str)) == ESP_OK) {
                led = atoi(led_str);
                if (led < 0 || led >= LED_COUNT) led = 0;
            }
            char test_str[4] = {0};
            bool is_test = (httpd_query_key_value(buf, "test", test_str, sizeof(test_str)) == ESP_OK && atoi(test_str) == 1);
            for (int i = 0; i < STATE_COUNT; i++) {
                if (strcmp(state, state_names[i]) == 0) {
                    if (is_test) {
                        test_mode_active = true;  // skip idle timeout, JS timer controls test lifecycle
                        if (led_str[0] == '\0') {
                            test_all_leds = true;
                            for (int n = 0; n < LED_COUNT; n++)
                                test_led_states[n] = (anim_state_t)i;
                        } else {
                            test_all_leds = false;
                            test_led_states[led] = (anim_state_t)i;
                        }
                        // All-LED OFF explicitly ends the test → real state rendering
                        if (i == STATE_OFF && led_str[0] == '\0') {
                            test_mode_active = false;
                            test_all_leds = false;
                        }
                    } else {
                        // Auto LED allocation via project name (multi-project mode)
                        char project[32] = {0};
                        char platform[16] = {0};
                        httpd_query_key_value(buf, "project", project, sizeof(project));
                        httpd_query_key_value(buf, "platform", platform, sizeof(platform));
                        int target_led = led;
                        if (multi_project_mode && project[0]) {
                            target_led = alloc_led_for_project(project, platform);
                            if (target_led < 0) { httpd_resp_sendstr(req, "busy"); return ESP_OK; }
                        }
                        if (multi_project_mode || target_led == 0 || project[0])
                            set_state_idx(target_led, (anim_state_t)i);
                        else
                            set_state((anim_state_t)i);
                        if (platform[0]) strncpy(platform_names[target_led], platform, sizeof(platform_names[target_led]) - 1);
                    }
                    httpd_resp_sendstr(req, "ok");
                    return ESP_OK;
                }
            }
        }
        // BLE on/off
        char ble[8];
        if (httpd_query_key_value(buf, "ble", ble, sizeof(ble)) == ESP_OK) {
            int v = atoi(ble);
            ble_enabled = (v != 0);
            save_ble_enabled_to_nvs(ble_enabled);
            if (ble_initialized) {
                if (ble_enabled) ble_runtime_enable();
                else             ble_runtime_disable();
            }
            httpd_resp_set_type(req, "application/json");
            char json[64];
            snprintf(json, sizeof(json), "{\"ble_enabled\":%s}", ble_enabled ? "true" : "false");
            httpd_resp_sendstr(req, json);
            return ESP_OK;
        }
        // BLE discoverable on/off
        char bledisc[8];
        if (httpd_query_key_value(buf, "bledisc", bledisc, sizeof(bledisc)) == ESP_OK) {
            int v = atoi(bledisc);
            if (!ble_enabled) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "BLE disabled"); return ESP_OK; }
            ble_discoverable = (v != 0);
            save_ble_discoverable_to_nvs(ble_discoverable);
            if (ble_discoverable && ble_initialized) {
                ble_start_advertising();
            } else if (!ble_discoverable && ble_initialized) {
                esp_ble_gap_stop_advertising();
            }
            httpd_resp_set_type(req, "application/json");
            char json[64];
            snprintf(json, sizeof(json), "{\"ble_discoverable\":%s}", ble_discoverable ? "true" : "false");
            httpd_resp_sendstr(req, json);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid");
    return ESP_OK;
}

// --- /brightness?b=128 ---
static esp_err_t handle_brightness(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "b", val, sizeof(val)) == ESP_OK) {
            int b = atoi(val);
            if (b < 1) b = 1;
            if (b > 255) b = 255;
            brightness = (uint8_t)b;
            save_brightness_to_nvs(brightness);
        }
    }
    char json[32];
    snprintf(json, sizeof(json), "{\"b\":%d}", brightness);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// --- JSON string escape (for SSID safety) ---
static void json_escape_ssid(char *dst, const char *src, size_t dst_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j < dst_len - 2) { dst[j++] = '\\'; dst[j++] = c; }
        } else if (c >= 0x20 && c <= 0x7E) {
            dst[j++] = c;
        } else {
            // Replace non-printable/UTF-8 with space
            if (j < dst_len - 1) dst[j++] = ' ';
        }
    }
    dst[j] = '\0';
}

// --- HTML entity escape ---
static void html_escape(char *dst, const char *src, size_t dst_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '&' && j < dst_len - 5) {
            dst[j++] = '&'; dst[j++] = 'a'; dst[j++] = 'm'; dst[j++] = 'p'; dst[j++] = ';';
        } else if (c == '<' && j < dst_len - 4) {
            dst[j++] = '&'; dst[j++] = 'l'; dst[j++] = 't'; dst[j++] = ';';
        } else if (c == '>' && j < dst_len - 4) {
            dst[j++] = '&'; dst[j++] = 'g'; dst[j++] = 't'; dst[j++] = ';';
        } else if (c == '"' && j < dst_len - 6) {
            dst[j++] = '&'; dst[j++] = 'q'; dst[j++] = 'u'; dst[j++] = 'o'; dst[j++] = 't'; dst[j++] = ';';
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

// --- URL decode helper ---
static void url_decode(char *dst, const char *src, size_t dst_len) {
    char *out = dst;
    const char *in = src;
    while (*in && (out - dst) < (int)(dst_len - 1)) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (*in == '+') {
            *out++ = ' ';
            in++;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
}

// --- /wifi?ssid=x&pwd=y ---
static esp_err_t handle_wifi(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char raw_ssid[33] = {0};
        char raw_pwd[65] = {0};
        if (httpd_query_key_value(buf, "ssid", raw_ssid, sizeof(raw_ssid)) == ESP_OK &&
            httpd_query_key_value(buf, "pwd", raw_pwd, sizeof(raw_pwd)) == ESP_OK) {
            char ssid[33] = {0};
            char pwd[65] = {0};
            url_decode(ssid, raw_ssid, sizeof(ssid));
            url_decode(pwd, raw_pwd, sizeof(pwd));
            if (strlen(ssid) > 0 && strlen(ssid) <= 32 && strlen(pwd) <= 64) {
                save_wifi_to_nvs(ssid, pwd);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"ok\":true}");
                ESP_LOGI(TAG, "WiFi credentials saved, restarting...");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
                return ESP_OK;
            }
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid input\"}");
    return ESP_OK;
}

// --- /lang?l=zh ---
static esp_err_t handle_lang(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char l[8] = {0};
        if (httpd_query_key_value(buf, "l", l, sizeof(l)) == ESP_OK) {
            if (strcmp(l, "en") == 0) {
                g_lang = 1;
                save_lang_to_nvs(1);
            } else {
                g_lang = 0;
                save_lang_to_nvs(0);
            }
        }
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// ------------------------------------------------------------------
// BLE runtime switch — fully stop/start bluedroid + controller
// ------------------------------------------------------------------
static void ble_runtime_disable(void) {
    esp_ble_gap_stop_advertising();
    esp_bluedroid_disable();
    esp_bt_controller_disable();
    ESP_LOGI(TAG, "BLE runtime disabled");
}

static void ble_runtime_enable(void) {
    esp_err_t e = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "BLE: re-enable controller failed: %s", esp_err_to_name(e));
        return;
    }
    e = esp_bluedroid_enable();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "BLE: re-enable bluedroid failed: %s", esp_err_to_name(e));
        return;
    }
    // Re-register callbacks and GATT app — bluedroid disable/enable
    // cycle clears previous registrations and device name.
    esp_ble_gap_set_device_name(ble_device_name);
    esp_ble_gatts_register_callback(ble_gatts_event_handler);
    esp_ble_gap_register_callback(ble_gap_event_handler);
    esp_ble_gatts_app_register(BLE_APP_ID);
    ble_start_advertising();
    ESP_LOGI(TAG, "BLE runtime enabled, GATT re-registered, advertising started");
}

// --- /ble?en=1|0 ---
static esp_err_t handle_ble(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char v[8];
        if (httpd_query_key_value(buf, "en", v, sizeof(v)) == ESP_OK) {
            bool en = (atoi(v) != 0);
            ble_enabled = en;
            save_ble_enabled_to_nvs(en);
            if (ble_initialized) {
                if (en) ble_runtime_enable();
                else    ble_runtime_disable();
            }
            char json[64];
            snprintf(json, sizeof(json), "{\"ble_enabled\":%s}", en ? "true" : "false");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing en");
    return ESP_OK;
}

// --- /bledisc?en=1|0 ---
static esp_err_t handle_bledisc(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char v[8];
        if (httpd_query_key_value(buf, "en", v, sizeof(v)) == ESP_OK) {
            bool en = (atoi(v) != 0);
            if (!ble_enabled) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "BLE disabled"); return ESP_OK; }
            ble_discoverable = en;
            save_ble_discoverable_to_nvs(en);
            if (ble_discoverable && ble_initialized) {
                ble_start_advertising();
            } else if (!ble_discoverable && ble_initialized) {
                esp_ble_gap_stop_advertising();
            }
            char json[64];
            snprintf(json, sizeof(json), "{\"ble_discoverable\":%s}", en ? "true" : "false");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing en");
    return ESP_OK;
}

// --- /ble/clearbonds ---
static esp_err_t handle_ble_clearbonds(httpd_req_t *req) {
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t *dev_list = malloc(dev_num * sizeof(esp_ble_bond_dev_t));
        if (dev_list) {
            esp_ble_get_bond_device_list(&dev_num, dev_list);
            for (int i = 0; i < dev_num; i++) {
                esp_ble_remove_bond_device(dev_list[i].bd_addr);
            }
            free(dev_list);
        }
    }
    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"removed\":%d}", dev_num);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}


// --- /idle_timeout?t=120 ---
static esp_err_t handle_idle_timeout(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "t", val, sizeof(val)) == ESP_OK) {
            int t = atoi(val);
            if (t < 0) t = 0;
            if (t > 3600) t = 3600;
            idle_timeout_ms = (uint32_t)t * 1000;
            save_idle_timeout_to_nvs(idle_timeout_ms);
            char json[64];
            snprintf(json, sizeof(json), "{\"ok\":true,\"idle_timeout\":%lu}",
                     (unsigned long)(idle_timeout_ms / 1000));
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, json);
            ESP_LOGI(TAG, "Idle timeout set to %lu s", (unsigned long)(idle_timeout_ms / 1000));
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing t");
    return ESP_OK;
}

// Forward declare restart task
static void restart_task(void *arg);

// --- /static_ip?ip=x&gw=y&mask=z&en=1 ---
static esp_err_t handle_static_ip(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char ip[16] = {0}, gw[16] = {0}, mask[16] = {0}, en_str[4] = {0};
        if (httpd_query_key_value(buf, "ip", ip, sizeof(ip)) == ESP_OK &&
            httpd_query_key_value(buf, "gw", gw, sizeof(gw)) == ESP_OK &&
            httpd_query_key_value(buf, "mask", mask, sizeof(mask)) == ESP_OK) {
            httpd_query_key_value(buf, "en", en_str, sizeof(en_str));
            bool en = (en_str[0] == '1');
            strcpy(static_ip, ip); strcpy(static_gw, gw); strcpy(static_mask, mask);
            use_static_ip = en && ip[0];
            save_static_ip_to_nvs(ip, gw, mask, use_static_ip);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":true}");
            ESP_LOGI(TAG, "Static IP: %s/%s/%s (en=%d), restarting...", ip, gw, mask, use_static_ip);
            xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing params");
    return ESP_OK;
}

// --- /ledmap — per-LED state array for dashboard ---
static esp_err_t handle_ledmap(httpd_req_t *req) {
    static char json[768];
    int off = 0;
    off += snprintf(json + off, sizeof(json) - off, "{\"l\":[");
    for (int n = 0; n < LED_COUNT; n++) {
        const char *label = project_names[n][0] ? project_names[n] : "";
        const char *plat = platform_names[n][0] ? platform_names[n] : "";
        off += snprintf(json + off, sizeof(json) - off,
                        "%s{\"s\":\"%s\",\"l\":\"%s\",\"p\":\"%s\"}",
                        (n > 0 ? "," : ""),
                        state_names[led_states[n]], label, plat);
    }
    off += snprintf(json + off, sizeof(json) - off, "],\"multi\":%s}",
                    multi_project_mode ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// --- /mode — switch between single-strip and multi-LED mode ---


static esp_err_t handle_mode(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char m[4] = {0};
        if (httpd_query_key_value(buf, "m", m, sizeof(m)) == ESP_OK) {
            multi_project_mode = (atoi(m) != 0);
            save_multi_project_to_nvs(multi_project_mode);
            httpd_resp_set_type(req, "application/json");
            char json[32];
            snprintf(json, sizeof(json), "{\"multi_led\":%s}", multi_project_mode ? "true" : "false");
            httpd_resp_sendstr(req, json);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing m");
    return ESP_OK;
}

// --- /led_reverse?r=1|0 ---
static esp_err_t handle_led_reverse(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char r[4] = {0};
        if (httpd_query_key_value(buf, "r", r, sizeof(r)) == ESP_OK) {
            led_reverse = (atoi(r) != 0);
            save_led_reverse_to_nvs(led_reverse);
            httpd_resp_set_type(req, "application/json");
            char json[32];
            snprintf(json, sizeof(json), "{\"led_reverse\":%s}", led_reverse ? "true" : "false");
            httpd_resp_sendstr(req, json);
            ESP_LOGI(TAG, "LED reverse: %s", led_reverse ? "ON" : "OFF");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing r");
    return ESP_OK;
}

// --- /scan — return nearby WiFi list as JSON ---
static esp_err_t handle_scan(httpd_req_t *req) {
    // Check WiFi state
    wifi_mode_t current_mode;
    esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
    ESP_LOGI(TAG, "Scan request: mode=%d, err=%s", current_mode, esp_err_to_name(mode_err));

    // WiFi scan requires STA or APSTA mode
    if (current_mode != WIFI_MODE_APSTA && current_mode != WIFI_MODE_STA) {
        ESP_LOGW(TAG, "Scan failed: WiFi mode %d not supported", current_mode);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    // Temporarily disable BLE to avoid RF conflict during scan
    bool ble_was_enabled = ble_enabled;
    if (ble_was_enabled && ble_initialized) {
        ESP_LOGI(TAG, "Disabling BLE for WiFi scan...");
        esp_ble_gap_stop_advertising();
        esp_bluedroid_disable();
        esp_bt_controller_disable();
        vTaskDelay(pdMS_TO_TICKS(100));  // Allow RF to switch
    }

    // When BLE is enabled, must use default active scan parameters
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,  // Use default when BLE enabled
        .scan_time.active.max = 0,  // Use default when BLE enabled
    };
    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
        if (ble_was_enabled) ble_runtime_enable();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "WiFi scan found %d APs", ap_count);

    // Re-enable BLE with full GATT re-registration
    if (ble_was_enabled) ble_runtime_enable();

    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    wifi_ap_record_t *aps = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!aps) {
        ESP_LOGE(TAG, "Scan malloc failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&ap_count, aps);

    // Sort by RSSI descending (strongest first)
    for (int i = 0; i < ap_count - 1; i++) {
        for (int j = i + 1; j < ap_count; j++) {
            if (aps[j].rssi > aps[i].rssi) {
                wifi_ap_record_t tmp = aps[i];
                aps[i] = aps[j];
                aps[j] = tmp;
            }
        }
    }

    char *json = malloc(ap_count * 96 + 8);
    if (!json) {
        free(aps);
        ESP_LOGE(TAG, "Scan JSON malloc failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
        return ESP_OK;
    }

    int pos = snprintf(json, ap_count * 96 + 8, "[");
    for (int i = 0; i < ap_count; i++) {
        char escaped_ssid[65] = {0};
        json_escape_ssid(escaped_ssid, (const char *)aps[i].ssid, sizeof(escaped_ssid));
        pos += snprintf(json + pos, ap_count * 96 + 8 - pos,
            "%s{\"ssid\":\"%s\",\"rssi\":%d}",
            i > 0 ? "," : "", escaped_ssid, aps[i].rssi);
    }
    snprintf(json + pos, ap_count * 96 + 8 - pos, "]");
    free(aps);

    ESP_LOGI(TAG, "Scan JSON: %d bytes, sending response", pos);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static void restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}
// ===================================================================
// Common HTML chunks used by both / and /help
// ===================================================================

// get_lang_str() removed — unused
// typedef removed — unused

// ===================================================================
// / — Main page
// ===================================================================


static esp_err_t handle_dash(httpd_req_t *req) {
    resp_init();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    bool en = (g_lang == 1);

    RESP_SEND("<!DOCTYPE html><html lang=\""); RESP_SEND(en ? "en" : "zh-CN");
    RESP_SEND("\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>");
    RESP_SEND(en ? "Project Dashboard" : "项目看板");
    RESP_SEND("</title><style>"
        ":root{--bg:#121212;--surface:#1e1e1e;--text:#e0e0e0;--text-secondary:#a0a0a0;--accent:#bb86fc}"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'PingFang SC','Microsoft YaHei',sans-serif;padding:16px;min-height:100vh;display:flex;justify-content:center;align-items:flex-start}"
        ".card{background:var(--surface);border-radius:16px;padding:24px;max-width:480px;width:100%;box-shadow:0 4px 24px rgba(0,0,0,0.4)}"
        ".header{text-align:center;margin-bottom:24px}"
        ".header h1{font-size:1.3em;font-weight:600}"
        ".device-id{color:var(--accent);font-size:0.875em;font-family:monospace;margin-top:2px}"
        ".section{margin-bottom:20px}"
        ".section-title{font-size:0.875em;color:var(--text-secondary);margin-bottom:10px;text-transform:uppercase;letter-spacing:0.5px}"
        ".btn{display:inline-block;padding:6px 14px;border:1px solid #444;border-radius:8px;background:transparent;color:var(--accent);font-size:0.875em;cursor:pointer;text-decoration:none;transition:all 0.2s}"
        ".btn:hover{background:rgba(187,134,252,0.1)}"
        ".btn.back{display:block;text-align:center;margin-top:12px}"
        ".led-table{width:100%;border-collapse:collapse;font-size:0.95em}"
        ".led-table thead th{text-align:left;padding:8px 12px;color:var(--text-secondary);font-weight:500;font-size:0.9em;border-bottom:1px solid #333}"
        ".led-table thead th:first-child{width:40px;text-align:center}"
        ".led-table thead th:last-child{width:100px}"
        ".led-table tbody td{padding:14px 12px;border-bottom:1px solid #2a2a2a;transition:none}"
        ".led-table tbody tr.active td{border-bottom-color:var(--accent)}"
        ".led-num{text-align:center;color:var(--text-secondary);font-size:0.95em}"
        ".led-label{font-weight:500;font-size:0.95em;max-width:120px;overflow:hidden;white-space:nowrap}"
        ".led-state-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;vertical-align:middle}"
        ".led-state-text{vertical-align:middle;font-size:0.95em}"
        ".dash-placeholder{text-align:center;padding:40px 20px;color:var(--text-secondary);border:1px dashed #444;border-radius:12px}"
        ".dash-placeholder .icon{font-size:3em;margin-bottom:12px}"
        "</style></head><body>");

    // --- Active dashboard ---
    RESP_SEND("<div class=\"card\" id=\"dashActive\">"
        "<div class=\"header\">"
        "<h1>");
    RESP_SEND(en ? "Project Dashboard" : "项目看板");
    RESP_SEND("</h1>"
        "<div class=\"device-id\">");
    RESP_SEND(g_hostname);
    RESP_SEND("</div></div>"
        "<div class=\"section\">"
        "<div class=\"section-title\">");
    RESP_SEND(en ? "Project Status" : "项目状态");
    RESP_SEND("</div>"
        "<table class=\"led-table\">"
        "<thead><tr><th>#</th><th>");
    RESP_SEND(en ? "Platform" : "平台");
    RESP_SEND("</th><th>");
    RESP_SEND(en ? "Project" : "项目");
    RESP_SEND("</th><th>");
    RESP_SEND(en ? "Status" : "状态");
    RESP_SEND("</th></tr></thead>"
        "<tbody id=\"ledBody\"></tbody>"
        "</table></div>"
        "<a class=\"btn back\" href=\"/\">&larr; ");
    RESP_SEND(en ? "Back" : "返回");
    RESP_SEND("</a></div>");

    // --- Disabled placeholder ---
    RESP_SEND("<div class=\"card\" id=\"dashDisabled\" style=\"display:none\">"
        "<div class=\"header\"><h1>");
    RESP_SEND(en ? "Project Dashboard" : "项目看板");
    RESP_SEND("</h1></div>"
        "<div class=\"dash-placeholder\">"
        "<div class=\"icon\">&#x1F4A1;</div>"
        "<p>");
    RESP_SEND(en ? "Multi-project mode is off." : "当前是单项目模式。");
    RESP_SEND("</p><p style=\"font-size:0.875em;margin-top:4px\">");
    RESP_SEND(en ? "Enable it in Settings to see the dashboard." : "在设置中开启多项目模式后可查看。");
    RESP_SEND("</p><a class=\"btn back\" href=\"/\">");
    RESP_SEND(en ? "Back" : "返回");
    RESP_SEND("</a></div></div>");

    // --- JavaScript ---
    RESP_SEND("<script>"
        "var c={thinking:'#ff6b6b',coding:'#bb86fc',busy:'#ffaa00',waiting:'#e04040',success:'#4caf50',error:'#ff4444',alarm:'#ff0000',off:'#666'};"
        "function bld(){var t=document.getElementById('ledBody');if(!t)return;var h='';for(var i=0;i<8;i++)h+='<tr id=lr'+i+'><td class=led-num>'+(i+1)+'</td><td class=led-label id=lp'+i+'></td><td class=led-label id=ll'+i+'></td><td><span class=led-state-dot id=ld'+i+'></span><span class=led-state-text id=ls'+i+'>off</span></td></tr>';t.innerHTML=h;}"
        "function autoScroll(el){if(!el)return;clearInterval(el._tid);if(el.scrollWidth<=el.clientWidth)return;var d=1,t=0;el.scrollLeft=0;el._tid=setInterval(function(){t+=30;if(t>2000){d=-d;t=0}el.scrollLeft+=d},30)}"
        "function upd(d){var t=document.getElementById('ledBody');if(!t.querySelectorAll('tr').length)bld();for(var i=0;i<8;i++){var r=document.getElementById('lr'+i);if(!r)continue;var e=(d.l||[])[i]||{};var s=e.s||'off';var lb=e.l||'';var lp=e.p||'';var pl=document.getElementById('ll'+i);var pp=document.getElementById('lp'+i);pl.textContent=lb;pp.textContent=lp;autoScroll(pl);autoScroll(pp);document.getElementById('ls'+i).textContent=s;document.getElementById('ld'+i).style.backgroundColor=c[s]||'#666';r.style.borderBottom=lb?'2px solid rgba(187,134,252,0.35)':'';r.className=s!=='off'?'active':'';}}"
        "async function pollDash(){try{var r=await fetch('/status');var d=await r.json();"
        "if(d.multi_led){document.getElementById('dashActive').style.display='';document.getElementById('dashDisabled').style.display='none';var r2=await fetch('/ledmap');upd(await r2.json());}"
        "else{document.getElementById('dashActive').style.display='none';document.getElementById('dashDisabled').style.display='';}"
        "}catch(e){}}"
        "bld();setInterval(pollDash,2000);pollDash();"
        "</script></body></html>");

    resp_flush(req);
    return ESP_OK;
}

static esp_err_t handle_root(httpd_req_t *req) {
    resp_init();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // Check ?lang= parameter
    char query_buf[32];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char lang[4] = {0};
        if (httpd_query_key_value(query_buf, "lang", lang, sizeof(lang)) == ESP_OK) {
            if (strcmp(lang, "en") == 0) {
                g_lang = 1;
                save_lang_to_nvs(1);
            } else if (strcmp(lang, "zh") == 0) {
                g_lang = 0;
                save_lang_to_nvs(0);
            }
        }
    }
    bool en = (g_lang == 1);
    char ip[16] = {0};
    get_ip_str(ip, sizeof(ip));
    if (ip[0] == 0 && is_ap_mode) snprintf(ip, sizeof(ip), "%s", AP_IP);

    RESP_SEND("<!DOCTYPE html><html lang=\""); RESP_SEND(en ? "en" : "zh-CN");
    RESP_SEND("\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>");
    RESP_SEND(en ? "Vibe Coding AI Status LED" : "Vibe Coding AI 状态指示灯");
    RESP_SEND("</title><style>"
        ":root{--bg:#121212;--surface:#1e1e1e;--text:#e0e0e0;--text-secondary:#a0a0a0;--accent:#bb86fc;}"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'PingFang SC','Microsoft YaHei',sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}"
        ".card{background:var(--surface);border-radius:16px;padding:24px;max-width:420px;width:100%;box-shadow:0 4px 24px rgba(0,0,0,0.4)}"
        ".header{text-align:center;margin-bottom:24px}"
        ".header h1{font-size:1.3em;font-weight:600;margin-bottom:4px}"
        ".device-id{color:var(--accent);font-size:0.78em;font-family:monospace;margin-top:2px}"
        ".section{margin-bottom:20px}"
        ".section-title{font-size:0.85em;color:var(--text-secondary);margin-bottom:10px;text-transform:uppercase;letter-spacing:0.5px}"
        ".wifi-row{display:flex;align-items:center;gap:10px;padding:12px;background:rgba(255,255,255,0.04);border-radius:10px}"
        ".wifi-icon{font-size:1.2em;flex-shrink:0}"
        ".wifi-info{flex:1;min-width:0}"
        ".wifi-ssid{font-weight:600;font-size:0.95em}"
        ".wifi-ip{font-size:0.8em;color:var(--text-secondary)}"
        ".wifi-edit{margin-left:auto;flex-shrink:0}"
        ".btn{display:inline-block;padding:6px 14px;border:1px solid #444;border-radius:8px;background:transparent;color:var(--accent);font-size:0.82em;cursor:pointer;text-decoration:none;transition:all 0.2s;white-space:nowrap;flex-shrink:0}"
        ".btn:hover{background:rgba(187,134,252,0.1)}"
        ".btn.danger{border-color:#ff6b6b;color:#ff6b6b}"
        ".btn.danger:hover{background:rgba(255,107,107,0.1)}"
        ".btn.counting{border-color:#ffaa00;color:#ffaa00}"
        ".btn.dash{display:block;text-align:center;padding:10px;margin-top:6px;border:1px dashed var(--accent);border-radius:10px;font-size:0.82em}"
        ".toggle-row{display:flex;align-items:center;justify-content:space-between;padding:10px 12px;background:rgba(255,255,255,0.04);border-radius:10px;margin-bottom:8px;gap:8px}"
        ".toggle-label{font-size:0.9em;font-weight:500}"
        ".toggle-desc{font-size:0.75em;color:var(--text-secondary);margin-top:2px}"
        ".toggle-switch{position:relative;display:inline-block;width:48px;height:26px;flex-shrink:0}"
        ".toggle-switch input{opacity:0;width:0;height:0}"
        ".toggle-slider{position:absolute;cursor:pointer;inset:0;background:#444;border-radius:26px;transition:0.3s}"
        ".toggle-slider:before{content:'';position:absolute;height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:0.3s}"
        ".toggle-switch input:checked+.toggle-slider{background:var(--accent)}"
        ".toggle-switch input:checked+.toggle-slider:before{transform:translateX(22px)}"
        ".slider-row{display:flex;align-items:center;gap:12px}"
        "input[type=range]{flex:1;-webkit-appearance:none;height:6px;background:#333;border-radius:3px;outline:none}"
        "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:var(--accent);cursor:pointer;border:2px solid #1e1e1e;box-shadow:0 0 8px rgba(187,134,252,0.4)}"
        ".slider-val{font-size:1.2em;font-weight:700;min-width:40px;text-align:right;color:var(--accent)}"
        ".slider-labels{display:flex;justify-content:space-between;font-size:0.75em;color:var(--text-secondary);margin-top:4px}"
        ".state-btns{display:flex;flex-wrap:wrap;gap:8px;justify-content:center}"
        ".state-btn{padding:10px 18px;border:1px solid #444;border-radius:10px;color:var(--text);text-decoration:none;font-size:0.85em;cursor:pointer;transition:all 0.2s;background:transparent}"
        ".state-btn:hover{background:rgba(255,255,255,0.06);border-color:#666}"
        ".state-btn.active{border-color:var(--accent);color:var(--accent);background:rgba(187,134,252,0.1)}"
        ".status-row{display:flex;align-items:center;gap:12px;padding:12px;background:rgba(255,255,255,0.04);border-radius:10px}"
        ".status-dot{width:14px;height:14px;border-radius:50%;flex-shrink:0;box-shadow:0 0 10px currentColor}"
        ".status-name{font-size:1.05em;font-weight:600}"
        ".status-desc{font-size:0.78em;color:var(--text-secondary);margin-left:auto}"
        ".help-link{display:block;text-align:center;padding:10px;color:var(--accent);text-decoration:none;font-size:0.85em;border:1px solid #333;border-radius:10px;margin-top:12px;transition:background 0.2s}"
        ".help-link:hover{background:rgba(187,134,252,0.1)}"
        ".modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.7);z-index:10;align-items:center;justify-content:center}"
        ".modal-overlay.show{display:flex}"
        ".modal{background:var(--surface);border-radius:16px;padding:24px;max-width:380px;width:90%;box-shadow:0 8px 32px rgba(0,0,0,0.6)}"
        ".modal h2{font-size:1.2em;margin-bottom:16px;font-weight:600}"
        ".modal .warn{font-size:0.78em;color:#ffaa00;margin-bottom:12px}"
        ".modal label{display:block;font-size:0.85em;color:var(--text-secondary);margin-bottom:4px;margin-top:12px}"
        ".modal input[type=text],.modal input[type=password]{width:100%;padding:10px 12px;background:#2a2a2a;border:1px solid #444;border-radius:8px;color:var(--text);font-size:0.95em;outline:none}"
        ".modal input:focus{border-color:var(--accent)}"
        "#scanResults{max-height:150px;overflow-y:auto;margin:6px 0 0 0;font-size:0.8em;border:1px solid #2a2a2a;border-radius:6px;padding:4px 0}"
        ".scan-item{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".modal .btn-row{display:flex;gap:10px;margin-top:20px;justify-content:flex-end}"
        ".lang-switch{display:flex;justify-content:center;gap:8px;margin-top:16px}"
        ".lang-switch a{color:var(--text-secondary);text-decoration:none;font-size:0.82em;padding:4px 10px;border-radius:6px;transition:all 0.2s}"
        ".lang-switch a.active{color:var(--accent);background:rgba(187,134,252,0.15)}"
        ".lang-switch a:hover{color:var(--text)}"
        ".btn.success{border-color:#4caf50;color:#4caf50}"
        ".btn.success:hover{background:rgba(76,175,80,0.1)}"
        ".param-row{display:flex;align-items:center;justify-content:space-between;padding:10px 12px;background:rgba(255,255,255,0.04);border-radius:10px;margin-bottom:8px}"
        ".param-label{font-size:0.9em;font-weight:500}"
        ".param-desc{font-size:0.75em;color:var(--text-secondary);margin-top:2px}"
        ".param-input{width:80px;padding:6px 10px;background:#2a2a2a;border:1px solid #444;border-radius:8px;color:var(--text);font-size:0.9em;text-align:center;outline:none}"
        ".param-input:focus{border-color:var(--accent)}"
        ".param-unit{font-size:0.82em;color:var(--text-secondary);margin-left:6px}"
        ".toast{position:fixed;bottom:30px;left:50%;transform:translateX(-50%);background:var(--surface);border:1px solid #444;border-radius:10px;padding:10px 20px;font-size:0.88em;color:var(--text);z-index:20;opacity:0;transition:opacity 0.3s;pointer-events:none}"
        ".toast.show{opacity:1}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<div class=\"header\">"
        "<h1>");

    RESP_SEND(en ? "Vibe Coding AI Status LED" : "Vibe Coding AI 状态指示灯");
    RESP_SEND(" <small style=\"color:var(--text-secondary);font-size:0.55em;font-weight:400\">v3.5.1</small></h1>");
    RESP_SEND("<div class=\"device-id\">");
    RESP_SEND(g_hostname);
    RESP_SEND("</div></div>");

    // --- Network section ---
    RESP_SEND("<div class=\"section\"><div class=\"section-title\">");
    RESP_SEND(en ? "Network" : "网络连接");
    RESP_SEND("</div><div class=\"wifi-row\">"
        "<span class=\"wifi-icon\">&#x1F4F6;</span>"
        "<div class=\"wifi-info\">"
        "<div class=\"wifi-ssid\">");
    if (wifi_ssid[0]) {
        char esc[132] = {0};
        html_escape(esc, wifi_ssid, sizeof(esc));
        RESP_SEND(esc);
    } else {
        RESP_SEND(en ? "(AP Mode)" : "(AP 模式)");
    }
    RESP_SEND("</div><div class=\"wifi-ip\">");
    RESP_SEND(ip);
    RESP_SEND("</div></div>"
        "<a class=\"btn wifi-edit\" href=\"javascript:void(0)\" onclick=\"toggleWifiModal()\">");
    RESP_SEND(en ? "Change" : "修改");
    RESP_SEND("</a></div></div>");

    // --- Brightness slider ---
    RESP_SEND("<div class=\"section\"><div class=\"section-title\">");
    RESP_SEND(en ? "Brightness" : "亮度调节");
    RESP_SEND("</div><div class=\"slider-row\">"
        "<input type=\"range\" id=\"bri\" min=\"1\" max=\"255\" value=\"");
    char numbuf[8];
    snprintf(numbuf, sizeof(numbuf), "%d", brightness);
    RESP_SEND(numbuf);
    RESP_SEND("\"><span class=\"slider-val\" id=\"briVal\">");
    int bri_pct = (int)((float)brightness / 2.55f + 0.5f);
    snprintf(numbuf, sizeof(numbuf), "%d%%", bri_pct);
    RESP_SEND(numbuf);
    RESP_SEND("</span></div><div class=\"slider-labels\"><span>");
    RESP_SEND(en ? "Dim" : "暗");
    RESP_SEND("</span><span>");
    RESP_SEND(en ? "Bright" : "亮");
    RESP_SEND("</span></div></div>");

    // --- Bluetooth section ---
    RESP_SEND("<div class=\"section\"><div class=\"section-title\">");
    RESP_SEND(en ? "Bluetooth" : "蓝牙设置");
    RESP_SEND("</div>"
        "<div class=\"toggle-row\">"
        "<div><div class=\"toggle-label\">");
    RESP_SEND(en ? "BLE Server" : "蓝牙服务");
    RESP_SEND("</div><div class=\"toggle-desc\">");
    RESP_SEND(en ? "Enable BLE GATT communication" : "启用 BLE GATT 通信");
    RESP_SEND("</div></div>"
        "<label class=\"toggle-switch\">"
        "<input type=\"checkbox\" id=\"bleToggle\" onchange=\"toggleBLE(this.checked)\"");
    RESP_SEND(ble_enabled ? " checked" : "");
    RESP_SEND("><span class=\"toggle-slider\"></span></label>"
        "</div>"
        "<div class=\"toggle-row\" id=\"blediscRow\" style=\"opacity:");
    RESP_SEND(ble_enabled ? "1" : "0.5");
    RESP_SEND("\">"
        "<div><div class=\"toggle-label\">");
    RESP_SEND(en ? "Discoverable" : "蓝牙发现");
    RESP_SEND("</div><div class=\"toggle-desc\" id=\"blediscDesc\">");
    RESP_SEND(en ? "Visible to BLE scanners (120s auto-off)" : "允许 BLE 扫描发现（120秒自动关闭）");
    RESP_SEND("</div></div>"
        "<a class=\"btn\" id=\"blediscBtn\" href=\"javascript:void(0)\" onclick=\"startBLEDiscover()\">");
    RESP_SEND(en ? "Enable" : "开启发现");
    RESP_SEND("</a></div>"
        "<div class=\"toggle-row\" style=\"margin-top:8px\">"
        "<div><div class=\"toggle-label\">");
    RESP_SEND(en ? "Pairing" : "配对管理");
    RESP_SEND(" <span id=\"bondStatus\" style=\"color:var(--text-secondary);font-weight:400\">(");
    RESP_SEND(en ? "loading..." : "加载中...");
    RESP_SEND(")</span></div><div class=\"toggle-desc\">");
    RESP_SEND(en ? "Clear all bonded devices" : "清除所有已绑定的配对设备");
    RESP_SEND("</div></div>"
        "<button class=\"btn\" onclick=\"clearBonds()\">");
    RESP_SEND(en ? "Clear Bonds" : "清除配对");
    RESP_SEND("</button></div></div>");

    // --- Current status display ---
    const char *zh_names[] = {
        "思考中", "编码中", "执行中", "等待输入",
        "完成", "出错", "警告", "待机"
    };

    // --- Idle timeout ---
    char idle_buf[8];
    snprintf(idle_buf, sizeof(idle_buf), "%lu", (unsigned long)(idle_timeout_ms / 1000));
    RESP_SEND("<div class=\"section\"><div class=\"section-title\">");
    RESP_SEND(en ? "Auto Standby" : "自动待机");
    RESP_SEND("</div>"
        "<div class=\"param-row\">"
        "<div><div class=\"param-label\">");
    RESP_SEND(en ? "Idle Timeout" : "闲置超时");
    RESP_SEND("</div><div class=\"param-desc\">");
    RESP_SEND(en ? "0 = always on" : "0 = 常亮");
    RESP_SEND("</div></div>"
        "<div style=\"display:flex;align-items:center\">"
        "<input type=\"number\" class=\"param-input\" id=\"idleTimeout\" min=\"0\" max=\"3600\" value=\"");
    RESP_SEND(idle_buf);
    RESP_SEND("\"><span class=\"param-unit\">");
    RESP_SEND(en ? "s" : "秒");
    RESP_SEND("</span>"
        "<a class=\"btn success\" href=\"javascript:void(0)\" "
        "onclick=\"var v=document.getElementById('idleTimeout').value;fetch('/idle_timeout?t='+v)\" "
        "style=\"margin-left:8px;font-size:0.78em;padding:4px 10px\">");
    RESP_SEND(en ? "Save" : "保存");
    RESP_SEND("</a></div></div></div>");

    // --- LED direction toggle ---
    RESP_SEND("<div class=\"section\">"
        "<div class=\"toggle-row\">"
        "<div><div class=\"toggle-label\">");
    RESP_SEND(en ? "LED Direction" : "灯序方向");
    RESP_SEND("</div><div class=\"toggle-desc\">");
    RESP_SEND(en ? "Flip left-right LED order" : "翻转左右灯序方向");
    RESP_SEND("</div></div>"
        "<label class=\"toggle-switch\">");
    RESP_SEND("<input type=\"checkbox\" id=\"ledRevToggle\" onchange=\"(function(on){fetch('/led_reverse?r='+(on?'1':'0'));})(this.checked)\"");
    if (led_reverse) RESP_SEND(" checked");
    RESP_SEND("><span class=\"toggle-slider\"></span></label></div></div>");

    // --- Multi-project toggle (always visible) ---
    RESP_SEND("<div class=\"section\">"
        "<div class=\"toggle-row\">"
        "<div><div class=\"toggle-label\">");
    RESP_SEND(en ? "Multi-Project Mode" : "多项目模式");
    RESP_SEND("</div><div class=\"toggle-desc\">");
    RESP_SEND(en ? "Independent LED per project" : "每颗 LED 独立反映一个项目");
    RESP_SEND("</div></div>"
        "<label class=\"toggle-switch\">");
    RESP_SEND("<input type=\"checkbox\" id=\"multiToggle\" onchange=\"(function(on){fetch('/mode?m='+(on?'1':'0'));setTimeout(function(){location.reload()},200);})(this.checked)\"");
    if (multi_project_mode) RESP_SEND(" checked");
    RESP_SEND("><span class=\"toggle-slider\"></span></label></div></div>");

    // --- Status section: single-mode bar ↔ multi-mode LED grid (mutually exclusive) ---
    RESP_SEND("<div class=\"section\"><div class=\"section-title\">");
    RESP_SEND(en ? "Current Status" : "当前状态");
    RESP_SEND("</div>");

    // Single-mode status (hidden when multi_led on)
    RESP_SEND("<div id=\"singleStatus\" style=\"display:");
    RESP_SEND(multi_project_mode ? "none" : "block");
    RESP_SEND("\"><div class=\"status-row\">"
        "<div class=\"status-dot\" id=\"dot\"></div>"
        "<span class=\"status-name\" id=\"stateName\">");
    RESP_SEND(en ? state_names[led_states[0]] : zh_names[led_states[0]]);
    RESP_SEND("</span><span class=\"status-desc\" id=\"stateDesc\"></span>"
        "</div></div>");

    // Multi-mode LED grid (hidden when multi_led off)
    RESP_SEND("<div id=\"ledGrid\" style=\"display:");
    RESP_SEND(multi_project_mode ? "block" : "none");
    RESP_SEND("\">"
        "<div style=\"display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin:8px 0\">");
    for (int n = 0; n < LED_COUNT; n++) {
        RESP_SEND("<div id=\"led");
        char num[4]; snprintf(num, sizeof(num), "%d", n + 1); RESP_SEND(num);
        RESP_SEND("\" style=\"padding:6px 4px;border-radius:8px;text-align:center;");
        RESP_SEND("background:var(--surface);border:1px solid #333;font-size:0.7em\">");
        RESP_SEND("<div style=\"color:var(--text-secondary)\">LED ");
        RESP_SEND(num);
        RESP_SEND("</div><div style=\"margin-top:2px;color:");
        RESP_SEND(state_colors[led_states[n]]);
        RESP_SEND("\" class=\"led-state\">");
        RESP_SEND(state_names[led_states[n]]);
        RESP_SEND("</div></div>");
    }
    RESP_SEND("</div>"
        "<a class=\"btn dash\" href=\"/dash\">");
    RESP_SEND(en ? "Open Full Dashboard" : "打开完整看板");
    RESP_SEND("</a></div></div>");

    // --- Help link ---
    RESP_SEND("<a class=\"help-link\" href=\"/help?lang=");
    RESP_SEND(en ? "en" : "zh");
    RESP_SEND("\">");
    RESP_SEND(en ? "Status Guide" : "状态说明");
    RESP_SEND("</a>");

    // --- Language switch ---
    RESP_SEND("<div class=\"lang-switch\">"
        "<a href=\"/?lang=zh\" id=\"langZh\">");
    RESP_SEND("中文");
    RESP_SEND("</a><a href=\"/?lang=en\" id=\"langEn\">");
    RESP_SEND("English");
    RESP_SEND("</a></div></div>");

    // --- WiFi modal ---
    RESP_SEND("<div class=\"modal-overlay\" id=\"wifiModal\"><div class=\"modal\">"
        "<h2>");
    RESP_SEND(en ? "Change WiFi" : "修改 WiFi 设置");
    RESP_SEND("</h2><div class=\"warn\">");
    RESP_SEND(en ? "Device will restart after saving" : "修改后设备将重启并连接新网络");
    RESP_SEND("</div><label>");
    RESP_SEND(en ? "WiFi Name (SSID)" : "WiFi 名称 (SSID)");
    RESP_SEND("</label>"
        "<input type=\"text\" id=\"wifiSsid\" value=\"");
    char esc_ssid[132] = {0};
    html_escape(esc_ssid, wifi_ssid, sizeof(esc_ssid));
    RESP_SEND(esc_ssid);
    RESP_SEND("\" maxlength=\"32\"><label>");
    RESP_SEND(en ? "Password" : "密码");
    RESP_SEND("</label>"
        "<div style=\"position:relative;display:flex\">"
        "<input type=\"password\" id=\"wifiPwd\" value=\"\" placeholder=\"");
    RESP_SEND(en ? "leave blank to keep current" : "留空不修改");
    RESP_SEND("\" maxlength=\"64\" style=\"width:100%;padding-right:36px\">"
        "<span onclick=\"togglePwd()\" id=\"pwdToggle\" style=\"position:absolute;right:8px;top:50%;transform:translateY(-50%);cursor:pointer;font-size:1.1em;user-select:none;color:var(--text-secondary)\">&#x1f441;</span>"
        "</div>");
    RESP_SEND("<div style=\"display:flex;align-items:center;gap:8px;margin-top:8px\">"
        "<span style=\"font-size:0.82em;color:var(--text-secondary)\">");
    RESP_SEND(en ? "Nearby WiFi:" : "附近 WiFi:");
    RESP_SEND("</span>"
        "<a class=\"btn\" href=\"javascript:void(0)\" onclick=\"scanWifi()\" style=\"font-size:0.78em;padding:4px 10px\">");
    RESP_SEND(en ? "Scan" : "扫描");
    RESP_SEND("</a></div>"
        "<div id=\"scanResults\" style=\"max-height:150px;overflow-y:auto;margin-top:6px;font-size:0.8em\"></div>"
        "<div style=\"margin-top:12px\">"
        "<label style=\"display:flex;align-items:center;gap:8px;cursor:pointer\">"
        "<input type=\"checkbox\" id=\"useStaticIP\" onchange=\"toggleStaticFields()\" style=\"width:16px;height:16px\">"
        "<span>");
    RESP_SEND(en ? "Use static IP" : "使用固定 IP");
    RESP_SEND("</span></label>"
        "<div id=\"staticFields\" style=\"display:none;margin-top:8px\">"
        "<label style=\"font-size:0.85em;color:var(--text-secondary);display:block;margin-bottom:4px\">IP</label>"
        "<input type=\"text\" id=\"staticIp\" placeholder=\"192.168.1.100\" style=\"width:100%;padding:6px 8px;margin-bottom:8px;background:#2a2a2a;border:1px solid #444;border-radius:6px;color:#e0e0e0;font-size:0.9em\">"
        "<label style=\"font-size:0.85em;color:var(--text-secondary);display:block;margin-bottom:4px\">");
    RESP_SEND(en ? "Gateway" : "网关");
    RESP_SEND("</label>"
        "<input type=\"text\" id=\"staticGw\" placeholder=\"192.168.1.1\" style=\"width:100%;padding:6px 8px;margin-bottom:8px;background:#2a2a2a;border:1px solid #444;border-radius:6px;color:#e0e0e0;font-size:0.9em\">"
        "<label style=\"font-size:0.85em;color:var(--text-secondary);display:block;margin-bottom:4px\">");
    RESP_SEND(en ? "Netmask" : "子网掩码");
    RESP_SEND("</label>"
        "<input type=\"text\" id=\"staticMask\" value=\"255.255.255.0\" style=\"width:100%;padding:6px 8px;margin-bottom:8px;background:#2a2a2a;border:1px solid #444;border-radius:6px;color:#e0e0e0;font-size:0.9em\">"
        "</div></div>"
        "<div class=\"btn-row\">"
        "<a class=\"btn\" href=\"javascript:void(0)\" onclick=\"toggleWifiModal()\">");
    RESP_SEND(en ? "Cancel" : "取消");
    RESP_SEND("</a>"
        "<a class=\"btn\" href=\"javascript:void(0)\" onclick=\"saveWifi()\" style=\"border-color:#4caf50;color:#4caf50;\">");
    RESP_SEND(en ? "Save & Restart" : "保存并重启");
    RESP_SEND("</a></div></div></div>");

    // --- JavaScript ---
    RESP_SEND("<script>"
        // toggleMultiMode must be FIRST — called from onchange before script finishes
        "function toggleMultiMode(on){fetch('/mode?m='+(on?'1':'0'));location.reload();}"
        "function toggleWifiModal(){document.getElementById('wifiModal').classList.toggle('show');}"
        "function togglePwd(){var el=document.getElementById('wifiPwd');var btn=document.getElementById('pwdToggle');if(el.type==='password'){el.type='text';btn.innerHTML='&#x1f440;';}else{el.type='password';btn.innerHTML='&#x1f441;';}}"
        "function toggleStaticFields(){var f=document.getElementById('staticFields');var c=document.getElementById('useStaticIP');f.style.display=c.checked?'block':'none';}"
        "const pageLang='"); RESP_SEND(en ? "en" : "zh"); RESP_SEND("';"
        "const stateNamesZH=['思考中','编码中','执行中','等待输入','完成','出错','警告','待机'];"
        "const stateNamesEN=['thinking','coding','busy','waiting','success','error','alarm','off'];"
        "const stateDescsZH={thinking:'AI 正在思考/推理',coding:'AI 正在生成代码',busy:'正在执行工具/命令',waiting:'等待你的决策/输入',success:'任务成功完成',error:'执行过程中出错',alarm:'安全警告/权限请求',off:'空闲待机'};"
        "const stateDescsEN={thinking:'AI is thinking/reasoning',coding:'AI is generating code',busy:'Executing tool/command',waiting:'Waiting for your input',success:'Task completed',error:'An error occurred',alarm:'Security warning',off:'Device idle'};"
        "const colors={thinking:'#ff6b6b',coding:'#bb86fc',busy:'#ffaa00',waiting:'#e04040',success:'#4caf50',error:'#ff4444',alarm:'#ff0000',off:'#666'};"
        "const slider=document.getElementById('bri');"
        "const valSpan=document.getElementById('briVal');"
        "const dot=document.getElementById('dot');"
        "const stateName=document.getElementById('stateName');"
        "const stateDesc=document.getElementById('stateDesc');"
        "let lastState='';"
        "slider.addEventListener('input',function(){valSpan.textContent=Math.round(slider.value/2.55)+'%';});"
        "slider.addEventListener('change',function(){fetch('/brightness?b='+slider.value);});"
        "async function pollStatus(){try{const r=await fetch('/status');const d=await r.json();"
        "if(d.state!==lastState){lastState=d.state;"
        "dot.style.backgroundColor=colors[d.state]||'#666';dot.style.color=colors[d.state]||'#666';"
        "const names=pageLang==='en'?stateNamesEN:stateNamesZH;"
        "const descs=pageLang==='en'?stateDescsEN:stateDescsZH;"
        "stateName.textContent=names[d.state]||d.state;stateDesc.textContent=descs[d.state]||'';}"
        "slider.value=d.brightness;valSpan.textContent=Math.round(d.brightness/2.55)+'%';"
        "const bleEl=document.getElementById('bleToggle');"
        "const discRow=document.getElementById('blediscRow');"
        "if(bleEl)bleEl.checked=d.ble_enabled;"
        "if(discRow)discRow.style.opacity=d.ble_enabled?'1':'0.5';"
        "if(d.idle_timeout){const el=document.getElementById('idleTimeout');if(document.activeElement!==el)el.value=d.idle_timeout;}"
        "const bs=document.getElementById('bondStatus');if(bs){const n=d.ble_bond_count||0;"
        "bs.textContent=pageLang==='en'?(n?'(paired: '+n+')':'(unpaired)'):(n?'(已配对: '+n+' 台)':'(未配对)');}"
        "if(d.led_states){const single=document.getElementById('singleStatus');const grid=document.getElementById('ledGrid');"
        "if(single)single.style.display=d.multi_led?'none':'block';"
        "if(grid)grid.style.display=d.multi_led?'block':'none';}"
        "const mt=document.getElementById('multiToggle');if(mt)mt.checked=d.multi_led;"
        "for(let i=0;i<d.led_states.length;i++){const led=document.getElementById('led'+(i+1));if(led){const st=led.querySelector('.led-state');if(st){st.textContent=d.led_states[i];st.style.color=colors[d.led_states[i]]||'#666';}"
        "led.style.borderColor=d.led_states[i]!=='off'?colors[d.led_states[i]]||'#666':'#333';}}"
        "}catch(e){} }"
        "function showToast(msg){const t=document.getElementById('toast');if(!t){const d=document.createElement('div');d.className='toast';d.id='toast';document.body.appendChild(d);}const tt=document.getElementById('toast');tt.textContent=msg;tt.classList.add('show');setTimeout(function(){tt.classList.remove('show');},2500);}"
        "let bleDiscoverTimer=null;let bleDiscoverSeconds=0;"
        "function startBLEDiscover(){"
        "const btn=document.getElementById('blediscBtn');"
        "const desc=document.getElementById('blediscDesc');"
        "if(bleDiscoverTimer){clearInterval(bleDiscoverTimer);bleDiscoverTimer=null;bleDiscoverSeconds=0;"
        "btn.textContent=pageLang==='en'?'Enable':'开启发现';btn.className='btn';"
        "desc.textContent=pageLang==='en'?'Visible to BLE scanners (120s auto-off)':'允许 BLE 扫描发现（120秒自动关闭）';"
        "fetch('/bledisc?en=0');return;}"
        "bleDiscoverSeconds=120;btn.className='btn counting';"
        "btn.textContent='\u23F3 '+bleDiscoverSeconds+'s';"
        "desc.textContent=pageLang==='en'?'Discoverable \u2014 remaining':'正在广播 \u2014 剩余';"
        "fetch('/bledisc?en=1');"
        "bleDiscoverTimer=setInterval(function(){bleDiscoverSeconds--;"
        "if(bleDiscoverSeconds<=0){clearInterval(bleDiscoverTimer);bleDiscoverTimer=null;"
        "btn.textContent=pageLang==='en'?'Enable':'开启发现';btn.className='btn';"
        "desc.textContent=pageLang==='en'?'Visible to BLE scanners (120s auto-off)':'允许 BLE 扫描发现（120秒自动关闭）';}"
        "else{btn.textContent='\u23F3 '+bleDiscoverSeconds+'s';}},1000);}"
        // --- BLE toggle ---
        "function clearBonds(){"
        "var msg=pageLang==='en'?'Clear all bonded BLE devices?':'确认清除所有已配对的蓝牙设备？';"
        "if(!confirm(msg))return;"
        "fetch('/ble/clearbonds').then(r=>r.json()).then(d=>{"
        "showToast(pageLang==='en'?'Cleared '+d.removed+' device(s)':'已清除 '+d.removed+' 个配对设备');"
        "}).catch(function(e){showToast('Error: '+e);});}"
        "async function toggleBLE(on){"
        "try{const r=await fetch('/ble?en='+(on?'1':'0'));"
        "const row=document.getElementById('blediscRow');"
        "if(!on){row.style.opacity='0.5';if(bleDiscoverTimer){clearInterval(bleDiscoverTimer);bleDiscoverTimer=null;}"
        "document.getElementById('blediscBtn').textContent=pageLang==='en'?'Enable':'开启发现';"
        "document.getElementById('blediscBtn').className='btn';}"
        "else{row.style.opacity='1';}}catch(e){}}"
        "setInterval(pollStatus,2000);pollStatus();"
        "async function scanWifi(){"
        "const el=document.getElementById('scanResults');"
        "el.innerHTML='"); RESP_SEND(en ? "Scanning..." : "扫描中..."); RESP_SEND("';"
        "try{const r=await fetch('/scan');const aps=await r.json();"
        "if(!aps.length){el.innerHTML='"); RESP_SEND(en ? "No networks found" : "未找到网络"); RESP_SEND("';return;}"
        "let h='';"
        "aps.forEach(ap=>{"
        "let sig=ap.rssi>=-50?'\U0001F7E2':ap.rssi>=-70?'\U0001F7E1':'\U0001F534';"
        "let esc=ap.ssid.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');"
        "h+='<div class=\"scan-item\" data-ssid=\"'+esc+'\" style=\"padding:4px 8px;cursor:pointer;border-radius:4px\">'+sig+' '+esc+' <span style=\"color:var(--text-secondary);font-size:0.85em\">('+ap.rssi+' dBm)</span></div>';"
        "});"
        "el.innerHTML=h;"
        "el.querySelectorAll('.scan-item').forEach(function(item){"
        "item.addEventListener('click',function(){"
        "document.getElementById('wifiSsid').value=this.getAttribute('data-ssid');"
        "});"
        "item.addEventListener('mouseenter',function(){this.style.background='rgba(255,255,255,0.08)';});"
        "item.addEventListener('mouseleave',function(){this.style.background='';});"
        "});"
        "}catch(e){el.innerHTML='"); RESP_SEND(en ? "Scan failed" : "扫描失败"); RESP_SEND("';}"
        "}"
        "async function saveWifi(){"
        "const ssid=document.getElementById('wifiSsid').value.trim();"
        "const pwd=document.getElementById('wifiPwd').value;"
        "const useStatic=document.getElementById('useStaticIP').checked;"
        "let ok=true;"
        "if(ssid||pwd){"
        "try{const r=await fetch('/wifi?ssid='+encodeURIComponent(ssid)+'&pwd='+encodeURIComponent(pwd));"
        "const d=await r.json();if(!d.ok)ok=false;}catch(e){ok=false}"
        "}"
        "if(useStatic){"
        "const ip=document.getElementById('staticIp').value.trim();"
        "const gw=document.getElementById('staticGw').value.trim();"
        "const mask=document.getElementById('staticMask').value.trim();"
        "if(!ip||!gw){alert('IP and gateway required');return;}"
        "try{await fetch('/static_ip?ip='+encodeURIComponent(ip)+'&gw='+encodeURIComponent(gw)+'&mask='+encodeURIComponent(mask)+'&en=1');}catch(e){ok=false}"
        "}else if(document.getElementById('staticIp').value.trim()){"
        "try{await fetch('/static_ip?ip=0.0.0.0&gw=0.0.0.0&mask=0.0.0.0&en=0');}catch(e){}"
        "}"
        "if(ok){");
    RESP_SEND(en ? "alert('Saved, rebooting...')" : "alert('已保存，正在重启...')");
    RESP_SEND(";document.getElementById('wifiModal').classList.remove('show');"
        "setTimeout(function(){location.reload();},3000);}"
        "}"
        "document.getElementById('langZh').className=pageLang==='zh'?'active':'';"
        "document.getElementById('langEn').className=pageLang==='en'?'active':'';"
        "</script></body></html>");

    resp_flush(req);
    return ESP_OK;
}

static esp_err_t handle_help(httpd_req_t *req) {
    resp_init();
    // Check ?lang= parameter
    char query_buf[32];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char lang[4] = {0};
        if (httpd_query_key_value(query_buf, "lang", lang, sizeof(lang)) == ESP_OK) {
            if (strcmp(lang, "en") == 0) {
                g_lang = 1;
                save_lang_to_nvs(1);
            } else if (strcmp(lang, "zh") == 0) {
                g_lang = 0;
                save_lang_to_nvs(0);
            }
        }
    }

    bool en = (g_lang == 1);

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // --- Head with styles ---
    RESP_SEND("<!DOCTYPE html><html lang=\""); RESP_SEND(en ? "en" : "zh-CN");
    RESP_SEND("\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>");
    RESP_SEND(en ? "LED Status Guide" : "LED 状态说明");
    RESP_SEND("</title><style>"
        ":root{--bg:#121212;--surface:#1e1e1e;--text:#e0e0e0;--text-secondary:#a0a0a0;--accent:#bb86fc}"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'PingFang SC','Microsoft YaHei',sans-serif;padding:16px;min-height:100vh;display:flex;justify-content:center;align-items:flex-start}"
        ".card{background:var(--surface);border-radius:16px;padding:24px;max-width:560px;width:100%;box-shadow:0 4px 24px rgba(0,0,0,0.4)}"
        ".header{text-align:center;margin-bottom:20px}"
        ".header h1{font-size:1.3em;font-weight:600}"
        ".section{margin-bottom:20px}"
        ".section-title{font-size:0.875em;color:var(--text-secondary);margin-bottom:10px;text-transform:uppercase;letter-spacing:0.5px}"
        ".toggle-row{display:flex;align-items:center;justify-content:space-between;padding:10px 12px;background:rgba(255,255,255,0.04);border-radius:10px;margin-bottom:8px;gap:8px}"
        ".toggle-label{font-size:0.9em;font-weight:500}"
        ".toggle-desc{font-size:0.875em;color:var(--text-secondary);margin-top:2px}"
        ".toggle-switch{position:relative;display:inline-block;width:48px;height:26px;flex-shrink:0}"
        ".toggle-switch input{opacity:0;width:0;height:0}"
        ".toggle-slider{position:absolute;cursor:pointer;inset:0;background:#444;border-radius:26px;transition:0.3s}"
        ".toggle-slider:before{content:'';position:absolute;height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:0.3s}"
        ".toggle-switch input:checked+.toggle-slider{background:var(--accent)}"
        ".toggle-switch input:checked+.toggle-slider:before{transform:translateX(22px)}"
        ".led-selector{display:flex;gap:8px;justify-content:center;margin:10px 0;display:none}"
        ".led-selector.show{display:flex}"
        ".led-dot-btn{width:32px;height:32px;border-radius:50%;border:2px solid #444;background:transparent;cursor:pointer;font-size:0.875em;color:var(--text-secondary);display:flex;align-items:center;justify-content:center;transition:all 0.2s}"
        ".led-dot-btn:hover{border-color:var(--accent);color:var(--text)}"
        ".led-dot-btn.selected{border-color:var(--accent);background:rgba(187,134,252,0.2);color:var(--accent)}"
        ".guide-table{width:100%;border-collapse:collapse;font-size:0.95em}"
        ".guide-table thead th{text-align:left;padding:6px 8px;color:var(--text-secondary);font-weight:500;font-size:0.9em;border-bottom:1px solid #333}"
        ".guide-table thead th:last-child{text-align:center;width:52px}"
        ".guide-table tbody td{padding:8px 6px;border-bottom:1px solid #2a2a2a;vertical-align:middle}"
        ".col-state{font-weight:600}"
        ".col-effect{font-size:0.95em;color:var(--text-secondary)}"
        ".col-desc{font-size:0.95em;color:var(--text-secondary)}"
        ".col-btn{text-align:center}"
        ".guide-dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px;vertical-align:middle}"
        ".preview-btn{padding:4px 12px;border:1px solid #444;border-radius:6px;background:transparent;color:var(--accent);font-size:0.875em;cursor:pointer;transition:all 0.2s;white-space:nowrap}"
        ".preview-btn:hover{background:rgba(187,134,252,0.1);border-color:var(--accent)}"
        ".btn{display:inline-block;padding:6px 14px;border:1px solid #444;border-radius:8px;background:transparent;color:var(--accent);font-size:0.875em;cursor:pointer;text-decoration:none;transition:all 0.2s;white-space:nowrap}"
        ".btn:hover{background:rgba(187,134,252,0.1)}"
        ".btn.back{display:block;text-align:center;margin-top:16px}"
        ".toast{position:fixed;bottom:30px;left:50%;transform:translateX(-50%);background:var(--surface);border:1px solid #444;border-radius:10px;padding:10px 20px;font-size:0.88em;color:var(--text);z-index:20;opacity:0;transition:opacity 0.3s;pointer-events:none}"
        ".toast.show{opacity:1}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<div class=\"header\"><h1>");
    RESP_SEND(en ? "LED Status Guide" : "LED 状态说明");
    RESP_SEND("</h1></div>");

    // --- Single-LED test mode toggle ---
    RESP_SEND("<div class=\"section\">"
        "<div class=\"toggle-row\">"
        "<div>"
        "<div class=\"toggle-label\">");
    RESP_SEND(en ? "Single-LED Test Mode" : "单灯测试模式");
    RESP_SEND("</div>"
        "<div class=\"toggle-desc\">");
    RESP_SEND(en ? "Select LED + click preview to control one LED" : "选择 LED + 点击预览，控制单颗灯珠");
    RESP_SEND("</div></div>"
        "<label class=\"toggle-switch\">"
        "<input type=\"checkbox\" id=\"singleToggle\" onchange=\"toggleSingleMode(this.checked)\">"
        "<span class=\"toggle-slider\"></span></label></div>"
        "<div class=\"led-selector\" id=\"ledSelector\">");
    for (int i = 0; i < 8; i++) {
        char ledbuf[128];
        snprintf(ledbuf, sizeof(ledbuf),
            "<button class=\"led-dot-btn%s\" onclick=\"selectLed(%d)\">%d</button>",
            (i == 0) ? " selected" : "", i, i + 1);
        RESP_SEND(ledbuf);
    }
    RESP_SEND("</div></div>");

    // --- Status table ---
    RESP_SEND("<div class=\"section\">"
        "<div class=\"section-title\">");
    RESP_SEND(en ? "Status Overview" : "状态一览");
    RESP_SEND("</div>"
        "<table class=\"guide-table\">"
        "<thead><tr><th>");
    RESP_SEND(en ? "Status" : "状态");
    RESP_SEND("</th><th>");
    RESP_SEND(en ? "Light Effect" : "灯光效果");
    RESP_SEND("</th><th>");
    RESP_SEND(en ? "Description" : "说明");
    RESP_SEND("</th><th>");
    RESP_SEND(en ? "Preview" : "预览");
    RESP_SEND("</th></tr></thead><tbody>");

    // Table data
    const struct {
        const char *status;
        const char *color;
        const char *effect_zh;
        const char *desc_zh;
        const char *effect_en;
        const char *desc_en;
    } rows[] = {
        {"thinking", "#ff6b6b",
         "高速彩虹旋转", "收到消息、AI 思考中",
         "High-speed rainbow spin", "Receiving message, AI thinking"},
        {"coding", "#bb86fc",
         "青紫液态呼吸", "编辑或写入文件",
         "Cyan-purple liquid breath", "Editing or writing files"},
        {"busy", "#ffaa00",
         "黄色间隔闪烁", "执行终端命令",
         "Yellow interval flash", "Executing terminal commands"},
        {"waiting", "#e04040",
         "红色呼吸", "等待用户确认",
         "Red breathing", "Waiting for user confirmation"},
        {"success", "#4caf50",
         "绿色呼吸", "任务完成",
         "Green breathing", "Task completed"},
        {"error", "#ff4444",
         "红橙三连快闪", "命令执行失败",
         "Red-orange triple flash", "Command execution failed"},
        {"alarm", "#ff0000",
         "红蓝翻转", "API 错误、异常告警",
         "Red-blue flip", "API error, exception alert"},
        {"off", "#666",
         "全灭", "会话结束、空闲待机",
         "All off", "Session ended, idle standby"},
    };

    for (int i = 0; i < 8; i++) {
        char rowbuf[640];
        snprintf(rowbuf, sizeof(rowbuf),
            "<tr>"
            "<td class=\"col-state\"><span class=\"guide-dot\" style=\"background:%s\"></span>%s</td>"
            "<td class=\"col-effect\">%s</td>"
            "<td class=\"col-desc\">%s</td>"
            "<td class=\"col-btn\"><button class=\"preview-btn\" onclick=\"preview('%s')\">%s</button></td>"
            "</tr>",
            rows[i].color,
            rows[i].status,
            en ? rows[i].effect_en : rows[i].effect_zh,
            en ? rows[i].desc_en : rows[i].desc_zh,
            rows[i].status,
            en ? "Test" : "测试");
        RESP_SEND(rowbuf);
    }

    RESP_SEND("</tbody></table></div>");

    // --- Back link ---
    RESP_SEND("<a class=\"btn back\" href=\"/\">&larr; ");
    RESP_SEND(en ? "Back" : "返回");
    RESP_SEND("</a></div>");

    // --- Toast ---
    RESP_SEND("<div class=\"toast\" id=\"toast\"></div>");

    // --- JavaScript ---
    RESP_SEND("<script>"
        "var selectedLed=0;"
        "var singleMode=false;"
        "var previewTimer=null;"
        "var previewActive=false;"
        "var savedStates=null;"
        // Set ALL 8 LEDs to a state (single request, no led param = all LEDs)
        "function setAllLEDs(state){fetch('/set?s='+state+'&test=1');}"
        // Save all LED states before entering test mode
        "async function saveAllStates(){try{var r=await fetch('/ledmap');var d=await r.json();savedStates=d.l.map(function(e){return e.s;});}catch(e){savedStates=null;}}"
        // Restore all saved LED states
        "function restoreAllStates(){if(!savedStates)return;for(var i=0;i<8;i++){var s=savedStates[i]||'off';fetch('/set?led='+i+'&s='+s+'&test=1');}}"
        "async function toggleSingleMode(on){"
        "singleMode=on;"
        "var sel=document.getElementById('ledSelector');"
        "sel.className=on?'led-selector show':'led-selector';"
        "if(on){await saveAllStates();setAllLEDs('off');}"
        "else{if(previewTimer){clearTimeout(previewTimer);previewTimer=null;}previewActive=false;restoreAllStates();savedStates=null;}"
        "}"
        "function selectLed(n){selectedLed=n;"
        "document.querySelectorAll('.led-dot-btn').forEach(function(b,i){b.className=i===n?'led-dot-btn selected':'led-dot-btn';});}"
        "function preview(state){"
        "if(previewTimer){clearTimeout(previewTimer);previewTimer=null;}"
        "var sm=document.getElementById('singleToggle').checked;"
        "previewActive=true;"
        "if(sm){fetch('/set?led='+selectedLed+'&s='+state+'&test=1');}"
        "else{setAllLEDs(state);}"
        "showToast((sm?'LED '+(selectedLed+1)+': ':'')+state+' (5s)');"
        "previewTimer=setTimeout(function(){"
        "previewActive=false;"
        "if(sm){fetch('/set?led='+selectedLed+'&s=off&test=1');}"
        "else{setAllLEDs('off');}"
        "previewTimer=null;"
        "},5000);"
        "}"
        "function showToast(m){var t=document.getElementById('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},2000);}"
        // Clean up when leaving the page
        "window.addEventListener('beforeunload',function(){"
        "if(previewTimer){clearTimeout(previewTimer);previewTimer=null;}"
        "if(singleMode&&savedStates){"
        "for(var i=0;i<8;i++){var s=savedStates[i]||'off';fetch('/set?led='+i+'&s='+s+'&test=1',{keepalive:true});}"
        "}else if(previewActive){"
        "fetch('/set?s=off&test=1',{keepalive:true});"
        "}"
        "});"
        "</script></body></html>");

    resp_flush(req);
    return ESP_OK;
}

// ===================================================================
// Start HTTP server
// ===================================================================

static void start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 17;

    esp_err_t ret = httpd_start(&server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        return;
    }

    httpd_uri_t root  = { .uri = "/",          .method = HTTP_GET, .handler = handle_root,       .user_ctx = NULL };
    httpd_uri_t help  = { .uri = "/help",      .method = HTTP_GET, .handler = handle_help,       .user_ctx = NULL };
    httpd_uri_t set   = { .uri = "/set",       .method = HTTP_GET, .handler = handle_set,        .user_ctx = NULL };
    httpd_uri_t stat  = { .uri = "/status",    .method = HTTP_GET, .handler = handle_status,     .user_ctx = NULL };
    httpd_uri_t bri   = { .uri = "/brightness",.method = HTTP_GET, .handler = handle_brightness, .user_ctx = NULL };
    httpd_uri_t wifi  = { .uri = "/wifi",      .method = HTTP_GET, .handler = handle_wifi,       .user_ctx = NULL };
    httpd_uri_t lang  = { .uri = "/lang",      .method = HTTP_GET, .handler = handle_lang,       .user_ctx = NULL };
    httpd_uri_t scan  = { .uri = "/scan",      .method = HTTP_GET, .handler = handle_scan,       .user_ctx = NULL };
    httpd_uri_t ble   = { .uri = "/ble",       .method = HTTP_GET, .handler = handle_ble,        .user_ctx = NULL };
    httpd_uri_t bledisc = { .uri = "/bledisc",  .method = HTTP_GET, .handler = handle_bledisc,    .user_ctx = NULL };
    httpd_uri_t clearbonds = { .uri = "/ble/clearbonds", .method = HTTP_GET, .handler = handle_ble_clearbonds, .user_ctx = NULL };
    httpd_uri_t idle_to = { .uri = "/idle_timeout", .method = HTTP_GET, .handler = handle_idle_timeout, .user_ctx = NULL };
    httpd_uri_t static_ip = { .uri = "/static_ip", .method = HTTP_GET, .handler = handle_static_ip, .user_ctx = NULL };
    httpd_uri_t ledmap   = { .uri = "/ledmap",   .method = HTTP_GET, .handler = handle_ledmap,   .user_ctx = NULL };
    httpd_uri_t dash     = { .uri = "/dash",     .method = HTTP_GET, .handler = handle_dash,     .user_ctx = NULL };
    httpd_uri_t mode_uri = { .uri = "/mode",     .method = HTTP_GET, .handler = handle_mode,     .user_ctx = NULL };
    httpd_uri_t led_rev  = { .uri = "/led_reverse", .method = HTTP_GET, .handler = handle_led_reverse, .user_ctx = NULL };
    httpd_register_uri_handler(server, &static_ip);
    httpd_register_uri_handler(server, &ledmap);
    httpd_register_uri_handler(server, &mode_uri);
    httpd_register_uri_handler(server, &led_rev);
    httpd_register_uri_handler(server, &dash);

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &help);
    httpd_register_uri_handler(server, &set);
    httpd_register_uri_handler(server, &stat);
    httpd_register_uri_handler(server, &bri);
    httpd_register_uri_handler(server, &wifi);
    httpd_register_uri_handler(server, &lang);
    httpd_register_uri_handler(server, &scan);
    httpd_register_uri_handler(server, &ble);
    httpd_register_uri_handler(server, &bledisc);
    httpd_register_uri_handler(server, &clearbonds);
    httpd_register_uri_handler(server, &idle_to);

    ESP_LOGI(TAG, "HTTP server started");
}

// ===================================================================
// Main
// ===================================================================

void app_main(void) {
    // 1. NVS init + load WiFi/brightness/lang
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    esp_phy_erase_cal_data_in_nvs();
    load_nvs_config();

    // 2. LED init
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));
    led_strip_clear(strip);

    // Read MAC once, used for BLE/AP/mDNS naming
    uint8_t base_mac[6];
    esp_efuse_mac_get_default(base_mac);
    snprintf(device_suffix, sizeof(device_suffix),
             "%02X%02X%02X%02X", base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
    snprintf(g_hostname, sizeof(g_hostname), "3dai-led-%s", device_suffix);
    for (int n = 0; n < LED_COUNT; n++) {
        led_states[n] = STATE_OFF;
        pending_states[n] = STATE_OFF;
        target_states[n] = STATE_OFF;
        pending_since_us_arr[n] = 0;
        project_names[n][0] = 0;
        platform_names[n][0] = 0;
        alloc_keys[n][0] = 0;
        test_led_states[n] = STATE_OFF;
    }

    // 3. BLE started later (after WiFi coexistence is stable)

    // 4. WiFi — official softap_sta pattern: AP + STA init together, no deinit
    esp_netif_init();
    esp_event_loop_create_default();

    // Create both netifs: AP always runs, STA connects if credentials exist
    esp_netif_create_default_wifi_ap();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_netif, g_hostname);

    // Configure AP — match official softap_sta example pattern
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%s", AP_SSID_PREFIX, device_suffix);
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "",
            .ssid_len = strlen(ap_ssid),
            .channel = 6,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
            .password = "",
            .ssid_hidden = 0,
            .beacon_interval = 100,
            .pmf_cfg = {
                .required = false,
            },
        }
    };
    memcpy(ap_cfg.ap.ssid, ap_ssid, ap_cfg.ap.ssid_len);

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);
    if (has_saved_wifi) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, wifi_ssid, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, wifi_pass, sizeof(sta_cfg.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    } else {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        // STA interface for scanning, no connect — no config set
    }

    // Register WiFi event handlers
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
        (esp_event_handler_t)wifi_disconnect_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        (esp_event_handler_t)wifi_got_ip_handler, NULL);

    esp_wifi_start();
    is_ap_mode = true;  // AP always starts first

    // STA: connect if credentials exist
    if (has_saved_wifi) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to %s (%d chars)...", wifi_ssid, strlen(wifi_ssid));
        // Wait up to 20s for connection
        for (int i = 0; i < 40; i++) {
            if (is_wifi_connected()) break;
            if (i == 0 || i == 10 || i == 20 || i == 30)
                ESP_LOGI(TAG, "  still waiting (%d/40)...", i+1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (is_wifi_connected()) {
        // STA connected — switch to STA-only, start mDNS
        is_ap_mode = false;
        esp_wifi_set_mode(WIFI_MODE_STA);
        mdns_init();
        mdns_hostname_set(g_hostname);
        mdns_service_add("3dai-led", "_http", "_tcp", 80, NULL, 0);
        char ip_str[16];
        get_ip_str(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "WiFi connected: %s, IP: %s", wifi_ssid, ip_str);
    } else {
        strcpy(wifi_ssid, ap_ssid);
        ESP_LOGW(TAG, "AP mode: %s, IP: %s", ap_ssid, AP_IP);
    }

    // 5. Start TCP server task
    xTaskCreate(tcp_server_task, "tcp_srv", 4096, NULL, 5, NULL);

    // 6. Start HTTP server
    start_webserver();

    char ip[16];
    get_ip_str(ip, sizeof(ip));
    ESP_LOGI(TAG, "WebUI: http://%s/", ip);
    ESP_LOGI(TAG, "TCP : port %d", TCP_PORT);

    // 6.5 Start BLE server — wait for WiFi to stabilize on shared antenna
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (ble_enabled) {
        ble_init_and_start();
        ESP_LOGI(TAG, "BLE device: %s", ble_device_name);
    }

    // 7. Idle timeout config
    if (idle_timeout_ms > 0) {
        ESP_LOGI(TAG, "Per-LED idle timeout: %lu s", (unsigned long)(idle_timeout_ms / 1000));
    } else {
        ESP_LOGI(TAG, "Idle timeout disabled (0 = always on)");
    }

    // 8. Animation loop
    while (1) {
        apply_pending_state();
        update_animation();
        vTaskDelay(pdMS_TO_TICKS(15));  // ~66 FPS
    }
}
