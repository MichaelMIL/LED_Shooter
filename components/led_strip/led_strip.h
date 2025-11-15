/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED strip configuration structure
 */
typedef struct {
    uint32_t resolution_hz;      ///< RMT resolution in Hz (default: 10000000 for 10MHz)
    int gpio_num;                ///< GPIO number for LED strip data line
    size_t led_count;            ///< Number of LEDs in the strip
    size_t mem_block_symbols;    ///< RMT memory block size in symbols (default: 64)
    uint32_t trans_queue_depth;  ///< Transaction queue depth (default: 4)
} led_strip_config_t;

/**
 * @brief LED strip handle
 */
typedef struct led_strip_t* led_strip_handle_t;

/**
 * @brief Initialize LED strip
 *
 * @param config Configuration structure
 * @param ret_strip Pointer to store the LED strip handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: Out of memory
 *      - ESP_FAIL: Failed to initialize
 */
esp_err_t led_strip_init(const led_strip_config_t *config, led_strip_handle_t *ret_strip);

/**
 * @brief Deinitialize LED strip
 *
 * @param strip LED strip handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_deinit(led_strip_handle_t strip);

/**
 * @brief Set pixel color (RGB) with brightness control
 *
 * @param strip LED strip handle
 * @param index LED index (0-based)
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @param brightness Brightness level (0-255, where 255 is full brightness)
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_set_pixel(led_strip_handle_t strip, size_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness);

/**
 * @brief Set pixel color from RGB array
 *
 * @param strip LED strip handle
 * @param index LED index (0-based)
 * @param rgb RGB array [R, G, B]
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_set_pixel_rgb(led_strip_handle_t strip, size_t index, const uint8_t *rgb);

/**
 * @brief Set all pixels to the same color
 *
 * @param strip LED strip handle
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_set_all(led_strip_handle_t strip, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Clear all pixels (set to black)
 *
 * @param strip LED strip handle
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_clear(led_strip_handle_t strip);

/**
 * @brief Refresh LED strip (send pixel data to LEDs)
 *
 * @param strip LED strip handle
 * @param timeout_ms Timeout in milliseconds (use portMAX_DELAY for infinite wait)
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_TIMEOUT: Timeout waiting for transmission
 */
esp_err_t led_strip_refresh(led_strip_handle_t strip, uint32_t timeout_ms);

/**
 * @brief Get pixel buffer pointer (for direct access)
 *
 * @param strip LED strip handle
 * @return Pointer to pixel buffer (RGB format, 3 bytes per LED)
 */
uint8_t* led_strip_get_pixel_buffer(led_strip_handle_t strip);

/**
 * @brief Get LED count
 *
 * @param strip LED strip handle
 * @return Number of LEDs
 */
size_t led_strip_get_led_count(led_strip_handle_t strip);

/**
 * @brief Rainbow chase animation configuration
 */
typedef struct {
    uint32_t frame_duration_ms;    ///< Duration of each frame in milliseconds (default: 20)
    float angle_inc_frame;          ///< Angle increment per frame (default: 0.02)
    float angle_inc_led;            ///< Angle increment per LED (default: 0.3)
    uint8_t brightness;             ///< Brightness level (0-255, default: 255)
} led_strip_rainbow_config_t;

/**
 * @brief Start rainbow chase animation as a FreeRTOS task
 *
 * @param strip LED strip handle
 * @param config Rainbow chase configuration (NULL for defaults)
 * @param task_handle Pointer to store the task handle (can be NULL)
 * @param stack_size Task stack size in bytes (default: 4096)
 * @param priority Task priority (default: 5)
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - ESP_ERR_NO_MEM: Out of memory
 */
esp_err_t led_strip_start_rainbow_chase(led_strip_handle_t strip, 
                                        const led_strip_rainbow_config_t *config,
                                        TaskHandle_t *task_handle,
                                        uint32_t stack_size,
                                        UBaseType_t priority);

/**
 * @brief Stop rainbow chase animation task
 *
 * @param task_handle Task handle returned from led_strip_start_rainbow_chase
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 */
esp_err_t led_strip_stop_rainbow_chase(TaskHandle_t task_handle);

#ifdef __cplusplus
}
#endif

