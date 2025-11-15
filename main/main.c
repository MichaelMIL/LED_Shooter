/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "led_strip.h"
#include "led_shooter_game.h"
#include "web_interface.h"

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
        .pattern_move_interval_ms = 500,
        .shot_speed_ms = 150,
        .brightness = 50,
    };

    led_shooter_game_handle_t game = NULL;
    ESP_ERROR_CHECK(led_shooter_game_init(&game_config, &game));

    ESP_LOGI(TAG, "Start game");
    ESP_ERROR_CHECK(led_shooter_game_start(game));

    // Initialize NVS (needed for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi in AP mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "LED_Shooter",
            .ssid_len = strlen("LED_Shooter"),
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: LED_Shooter");

    // Initialize web interface
    ESP_LOGI(TAG, "Initialize web interface");
    web_interface_config_t web_config = {
        .game = game,
        .port = 80,
    };

    web_interface_handle_t web_interface = NULL;
    ESP_ERROR_CHECK(web_interface_init(&web_config, &web_interface));
    ESP_LOGI(TAG, "Web interface ready. Connect to http://192.168.4.1");

    // Main task runs forever
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
