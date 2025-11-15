/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include <stdint.h>
#include "led_strip.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Color types for the game
 */
typedef enum {
    LED_SHOOTER_COLOR_RED = 0,
    LED_SHOOTER_COLOR_GREEN,
    LED_SHOOTER_COLOR_BLUE,
    LED_SHOOTER_COLOR_NONE
} led_shooter_color_t;

/**
 * @brief Game configuration structure
 */
typedef struct {
    led_strip_handle_t strip;          ///< LED strip handle
    int button_red_gpio;               ///< GPIO for red button
    int button_green_gpio;             ///< GPIO for green button
    int button_blue_gpio;               ///< GPIO for blue button
    size_t pattern_size;               ///< Number of colors in pattern (default: 10)
    uint32_t pattern_move_interval_ms; ///< Pattern movement interval in ms (default: 1000)
    uint32_t shot_speed_ms;            ///< Shot movement speed per LED in ms (default: 300)
    uint8_t brightness;                ///< LED brightness (0-255, default: 255)
} led_shooter_game_config_t;

/**
 * @brief Game handle
 */
typedef struct led_shooter_game_t* led_shooter_game_handle_t;

/**
 * @brief Initialize LED shooter game
 *
 * @param config Game configuration
 * @param ret_game Pointer to store the game handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: Out of memory
 */
esp_err_t led_shooter_game_init(const led_shooter_game_config_t *config, led_shooter_game_handle_t *ret_game);

/**
 * @brief Deinitialize LED shooter game
 *
 * @param game Game handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_shooter_game_deinit(led_shooter_game_handle_t game);

/**
 * @brief Start the game
 *
 * @param game Game handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_shooter_game_start(led_shooter_game_handle_t game);

/**
 * @brief Stop the game
 *
 * @param game Game handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_shooter_game_stop(led_shooter_game_handle_t game);

/**
 * @brief Trigger a shot programmatically
 *
 * @param game Game handle
 * @param color Color of the shot to fire
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: No available shot slots
 */
esp_err_t led_shooter_game_trigger_shot(led_shooter_game_handle_t game, led_shooter_color_t color);

/**
 * @brief Reset the game (restart pattern without stopping tasks)
 *
 * @param game Game handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_shooter_game_reset(led_shooter_game_handle_t game);

/**
 * @brief Get current game level
 *
 * @param game Game handle
 * @param level Pointer to store the level
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_shooter_game_get_level(led_shooter_game_handle_t game, uint32_t *level);

#ifdef __cplusplus
}
#endif

