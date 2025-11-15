/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "led_strip.h"

#define RMT_LED_STRIP_GPIO_NUM      16
#define EXAMPLE_LED_NUMBERS         63

static const char *TAG = "example";

void app_main(void)
{
    ESP_LOGI(TAG, "Initialize LED strip");
    led_strip_config_t strip_config = {
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .led_count = EXAMPLE_LED_NUMBERS,
        .resolution_hz = 10000000, // 10MHz resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    led_strip_handle_t led_strip = NULL;
    ESP_ERROR_CHECK(led_strip_init(&strip_config, &led_strip));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    // Configure rainbow chase animation
    led_strip_rainbow_config_t rainbow_config = {
        .frame_duration_ms = 20,
        .angle_inc_frame = 0.02f,
        .angle_inc_led = 0.3f,
        .brightness = 50,  // 50/255 brightness
    };

    TaskHandle_t rainbow_task_handle = NULL;
    ESP_ERROR_CHECK(led_strip_start_rainbow_chase(led_strip, &rainbow_config, &rainbow_task_handle, 0, 0));

    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_ERROR_CHECK(led_strip_stop_rainbow_chase(rainbow_task_handle));
    // Main task can now do other things while rainbow chase runs in background
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // You can add other code here that runs alongside the rainbow chase
    }
}
