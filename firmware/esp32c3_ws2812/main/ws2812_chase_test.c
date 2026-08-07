/*
 * WS2812 5050 RGB 8 灯珠 — 七彩跑马灯测试 (ESP-IDF 原生)
 * 设备: ESP32-C3 SuperMini (COM15)
 *
 * 接线:
 *   GPIO 8 → 100Ω → 灯板 DI
 *   5V     → 灯板 4.7VDC
 *   GND    → 灯板 GND
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"

#define LED_PIN     8
#define LED_COUNT   8
#define TAG         "ws2812"

static led_strip_handle_t led_strip;

// 7 色
static const struct { uint8_t r, g, b; } rainbow[7] = {
    {255,   0,   0},  // 红
    {255, 127,   0},  // 橙
    {255, 255,   0},  // 黄
    {  0, 255,   0},  // 绿
    {  0,   0, 255},  // 蓝
    { 75,   0, 130},  // 靛
    {148,   0, 211},  // 紫
};

static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num   = LED_PIN,
        .max_leds         = LED_COUNT,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma    = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "LED strip initialized: %d LEDs on GPIO %d", LED_COUNT, LED_PIN);
}

void app_main(void)
{
    ESP_LOGI(TAG, "WS2812 rainbow chase test");
    configure_led();

    // 初始全灭
    led_strip_clear(led_strip);

    int offset = 0;

    while (1) {
        // 每颗灯珠依次赋彩虹色
        for (int i = 0; i < LED_COUNT; i++) {
            int ci = (i + offset) % 7;
            led_strip_set_pixel(led_strip, i,
                rainbow[ci].r, rainbow[ci].g, rainbow[ci].b);
        }

        led_strip_refresh(led_strip);

        offset = (offset + 1) % 7;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
