/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_strip";

#define RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT 10000000 // 10MHz resolution, 1 tick = 0.1us
#define RMT_MEM_BLOCK_SYMBOLS_DEFAULT 64
#define RMT_TRANS_QUEUE_DEPTH_DEFAULT 4

// WS2812 timing constants
#define WS2812_T0H_US 0.3f
#define WS2812_T0L_US 0.9f
#define WS2812_T1H_US 0.9f
#define WS2812_T1L_US 0.3f
#define WS2812_RESET_US 50.0f

struct led_strip_t {
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t encoder;
    uint8_t *pixels;
    size_t led_count;
    uint32_t resolution_hz;
};

// WS2812 encoding symbols
static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = (uint32_t)(WS2812_T0H_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000),
    .level1 = 0,
    .duration1 = (uint32_t)(WS2812_T0L_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000),
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = (uint32_t)(WS2812_T1H_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000),
    .level1 = 0,
    .duration1 = (uint32_t)(WS2812_T1L_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000),
};

static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = (uint32_t)(WS2812_RESET_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000 / 2),
    .level1 = 0,
    .duration1 = (uint32_t)(WS2812_RESET_US * RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT / 1000000 / 2),
};

/**
 * @brief RMT encoder callback for WS2812
 */
static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // We need a minimum of 8 symbol spaces to encode a byte
    if (symbols_free < 8) {
        return 0;
    }

    // Calculate position in data from symbol position
    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t*)data;
    
    if (data_pos < data_size) {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            if (data_bytes[data_pos] & bitmask) {
                symbols[symbol_pos++] = ws2812_one;
            } else {
                symbols[symbol_pos++] = ws2812_zero;
            }
        }
        return symbol_pos;
    } else {
        // All bytes encoded, send reset signal
        symbols[0] = ws2812_reset;
        *done = 1;
        return 1;
    }
}

esp_err_t led_strip_init(const led_strip_config_t *config, led_strip_handle_t *ret_strip)
{
    if (config == NULL || ret_strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->led_count == 0) {
        ESP_LOGE(TAG, "LED count must be greater than 0");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate LED strip structure
    led_strip_handle_t strip = (led_strip_handle_t)malloc(sizeof(struct led_strip_t));
    if (strip == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LED strip structure");
        return ESP_ERR_NO_MEM;
    }
    memset(strip, 0, sizeof(struct led_strip_t));

    // Allocate pixel buffer
    strip->pixels = (uint8_t*)malloc(config->led_count * 3);
    if (strip->pixels == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        free(strip);
        return ESP_ERR_NO_MEM;
    }
    memset(strip->pixels, 0, config->led_count * 3);

    strip->led_count = config->led_count;
    strip->resolution_hz = config->resolution_hz ? config->resolution_hz : RMT_LED_STRIP_RESOLUTION_HZ_DEFAULT;

    // Create RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = config->gpio_num,
        .mem_block_symbols = config->mem_block_symbols ? config->mem_block_symbols : RMT_MEM_BLOCK_SYMBOLS_DEFAULT,
        .resolution_hz = strip->resolution_hz,
        .trans_queue_depth = config->trans_queue_depth ? config->trans_queue_depth : RMT_TRANS_QUEUE_DEPTH_DEFAULT,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &strip->rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel");
        free(strip->pixels);
        free(strip);
        return ret;
    }

    // Create encoder
    const rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback
    };
    ret = rmt_new_simple_encoder(&simple_encoder_cfg, &strip->encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT encoder");
        rmt_del_channel(strip->rmt_chan);
        free(strip->pixels);
        free(strip);
        return ret;
    }

    // Enable RMT channel
    ret = rmt_enable(strip->rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel");
        rmt_del_encoder(strip->encoder);
        rmt_del_channel(strip->rmt_chan);
        free(strip->pixels);
        free(strip);
        return ret;
    }

    *ret_strip = strip;
    ESP_LOGI(TAG, "LED strip initialized: %zu LEDs on GPIO %d", config->led_count, config->gpio_num);
    return ESP_OK;
}

esp_err_t led_strip_deinit(led_strip_handle_t strip)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_disable(strip->rmt_chan);
    rmt_del_encoder(strip->encoder);
    rmt_del_channel(strip->rmt_chan);
    free(strip->pixels);
    free(strip);

    ESP_LOGI(TAG, "LED strip deinitialized");
    return ESP_OK;
}

esp_err_t led_strip_set_pixel(led_strip_handle_t strip, size_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (index >= strip->led_count) {
        ESP_LOGE(TAG, "LED index %zu out of range (max: %zu)", index, strip->led_count - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Apply brightness scaling: multiply by brightness and divide by 255
    // Using 16-bit intermediate values to avoid overflow
    strip->pixels[index * 3 + 0] = (uint8_t)((red * brightness) / 255);
    strip->pixels[index * 3 + 1] = (uint8_t)((green * brightness) / 255);
    strip->pixels[index * 3 + 2] = (uint8_t)((blue * brightness) / 255);

    return ESP_OK;
}

esp_err_t led_strip_set_pixel_rgb(led_strip_handle_t strip, size_t index, const uint8_t *rgb)
{
    if (strip == NULL || rgb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (index >= strip->led_count) {
        ESP_LOGE(TAG, "LED index %zu out of range (max: %zu)", index, strip->led_count - 1);
        return ESP_ERR_INVALID_ARG;
    }

    strip->pixels[index * 3 + 0] = rgb[0];
    strip->pixels[index * 3 + 1] = rgb[1];
    strip->pixels[index * 3 + 2] = rgb[2];

    return ESP_OK;
}

esp_err_t led_strip_set_all(led_strip_handle_t strip, uint8_t red, uint8_t green, uint8_t blue)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < strip->led_count; i++) {
        strip->pixels[i * 3 + 0] = red;
        strip->pixels[i * 3 + 1] = green;
        strip->pixels[i * 3 + 2] = blue;
    }

    return ESP_OK;
}

esp_err_t led_strip_clear(led_strip_handle_t strip)
{
    return led_strip_set_all(strip, 0, 0, 0);
}

esp_err_t led_strip_refresh(led_strip_handle_t strip, uint32_t timeout_ms)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    esp_err_t ret = rmt_transmit(strip->rmt_chan, strip->encoder, 
                                  strip->pixels, strip->led_count * 3, &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit pixel data");
        return ret;
    }

    TickType_t timeout = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    ret = rmt_tx_wait_all_done(strip->rmt_chan, timeout);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wait for transmission completion");
        return ret;
    }

    return ESP_OK;
}

uint8_t* led_strip_get_pixel_buffer(led_strip_handle_t strip)
{
    if (strip == NULL) {
        return NULL;
    }
    return strip->pixels;
}

size_t led_strip_get_led_count(led_strip_handle_t strip)
{
    if (strip == NULL) {
        return 0;
    }
    return strip->led_count;
}

/**
 * @brief Structure to pass data to rainbow chase task
 */
typedef struct {
    led_strip_handle_t strip;
    led_strip_rainbow_config_t config;
} rainbow_chase_task_data_t;

/**
 * @brief Rainbow chase animation task
 */
static void rainbow_chase_task(void *pvParameters)
{
    rainbow_chase_task_data_t *data = (rainbow_chase_task_data_t *)pvParameters;
    led_strip_handle_t strip = data->strip;
    led_strip_rainbow_config_t config = data->config;
    
    // Free the task data structure as we no longer need it
    free(data);
    
    float offset = 0;
    size_t led_count = led_strip_get_led_count(strip);
    
    ESP_LOGI(TAG, "Rainbow chase task started");
    
    while (1) {
        for (size_t led = 0; led < led_count; led++) {
            // Build RGB pixels. Each color is an offset sine, which gives a
            // hue-like effect.
            float angle = offset + (led * config.angle_inc_led);
            const float color_off = (M_PI * 2) / 3;
            uint8_t red = (uint8_t)(sin(angle + color_off * 0) * 127 + 128);
            uint8_t green = (uint8_t)(sin(angle + color_off * 1) * 127 + 128);
            uint8_t blue = (uint8_t)(sin(angle + color_off * 2) * 117 + 128);
            ESP_ERROR_CHECK(led_strip_set_pixel(strip, led, red, green, blue, config.brightness));
        }
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(led_strip_refresh(strip, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(config.frame_duration_ms));
        // Increase offset to shift pattern
        offset += config.angle_inc_frame;
        if (offset > 2 * M_PI) {
            offset -= 2 * M_PI;
        }
    }
}

esp_err_t led_strip_start_rainbow_chase(led_strip_handle_t strip, 
                                        const led_strip_rainbow_config_t *config,
                                        TaskHandle_t *task_handle,
                                        uint32_t stack_size,
                                        UBaseType_t priority)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate task data structure
    rainbow_chase_task_data_t *data = (rainbow_chase_task_data_t *)malloc(sizeof(rainbow_chase_task_data_t));
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate task data");
        return ESP_ERR_NO_MEM;
    }

    data->strip = strip;
    
    // Use provided config or defaults
    if (config != NULL) {
        data->config = *config;
    } else {
        // Default values
        data->config.frame_duration_ms = 20;
        data->config.angle_inc_frame = 0.02f;
        data->config.angle_inc_led = 0.3f;
        data->config.brightness = 255;
    }

    // Use default stack size if 0
    if (stack_size == 0) {
        stack_size = 4096;
    }

    // Use default priority if 0
    if (priority == 0) {
        priority = 5;
    }

    // Create the task
    BaseType_t ret = xTaskCreate(rainbow_chase_task,
                                 "rainbow_chase",
                                 stack_size,
                                 data,
                                 priority,
                                 task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rainbow chase task");
        free(data);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Rainbow chase task created");
    return ESP_OK;
}

esp_err_t led_strip_stop_rainbow_chase(TaskHandle_t task_handle)
{
    if (task_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    vTaskDelete(task_handle);
    ESP_LOGI(TAG, "Rainbow chase task stopped");
    return ESP_OK;
}

