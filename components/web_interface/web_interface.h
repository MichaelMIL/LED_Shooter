/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include <stdint.h>
#include "led_shooter_game.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web interface configuration structure
 */
typedef struct {
    led_shooter_game_handle_t game;  ///< Game handle to trigger shots
    uint16_t port;                   ///< HTTP server port (default: 80)
} web_interface_config_t;

/**
 * @brief Web interface handle
 */
typedef struct web_interface_t* web_interface_handle_t;

/**
 * @brief Initialize web interface
 *
 * @param config Configuration structure
 * @param ret_interface Pointer to store the web interface handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: Out of memory
 */
esp_err_t web_interface_init(const web_interface_config_t *config, web_interface_handle_t *ret_interface);

/**
 * @brief Deinitialize web interface
 *
 * @param interface Web interface handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t web_interface_deinit(web_interface_handle_t interface);

#ifdef __cplusplus
}
#endif

