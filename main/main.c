/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "esp_log.h"
#include "led_strip.h"
#include "led_shooter_game.h"

#define RMT_LED_STRIP_GPIO_NUM      16
#define EXAMPLE_LED_NUMBERS         63

// Button GPIO pins - adjust these to match your hardware
#define BUTTON_RED_GPIO            0
#define BUTTON_GREEN_GPIO           4
#define BUTTON_BLUE_GPIO            5

static const char *TAG = "main";

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

    ESP_LOGI(TAG, "Initialize LED shooter game");
    led_shooter_game_config_t game_config = {
        .strip = led_strip,
        .button_red_gpio = BUTTON_RED_GPIO,
        .button_green_gpio = BUTTON_GREEN_GPIO,
        .button_blue_gpio = BUTTON_BLUE_GPIO,
        .pattern_size = 10,
        .pattern_move_interval_ms = 1000,
        .shot_speed_ms = 150,
        .brightness = 50,
    };

    led_shooter_game_handle_t game = NULL;
    ESP_ERROR_CHECK(led_shooter_game_init(&game_config, &game));

    ESP_LOGI(TAG, "Start game");
    ESP_ERROR_CHECK(led_shooter_game_start(game));

    // Main task runs forever
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
