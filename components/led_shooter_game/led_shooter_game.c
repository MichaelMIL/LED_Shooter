/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include <stdlib.h>
#include "led_shooter_game.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "led_shooter_game";

#define PATTERN_SIZE_DEFAULT 10
#define PATTERN_MOVE_INTERVAL_MS_DEFAULT 1000
#define SHOT_SPEED_MS_DEFAULT 300
#define BRIGHTNESS_DEFAULT 255
#define MAX_SHOTS 10  // Maximum number of simultaneous shots

// RGB color values
static const uint8_t color_rgb[3][3] = {
    {255, 0, 0},    // RED
    {0, 255, 0},    // GREEN
    {0, 0, 255}     // BLUE
};

struct led_shooter_game_t {
    led_strip_handle_t strip;
    int button_gpios[3];
    size_t led_count;
    size_t pattern_size;
    uint32_t pattern_move_interval_ms;
    uint32_t shot_speed_ms;
    uint8_t brightness;
    
    // Game state
    led_shooter_color_t pattern[PATTERN_SIZE_DEFAULT];
    size_t pattern_length;
    size_t pattern_position;  // Position of pattern start (furthest LED)
    
    // Multiple shots support
    struct {
        led_shooter_color_t color;
        int position;  // -1 means inactive
    } shots[MAX_SHOTS];
    size_t active_shots;
    
    // Tasks and queues
    TaskHandle_t game_task_handle;
    TaskHandle_t button_task_handle;
    QueueHandle_t button_queue;
    bool running;
};

/**
 * @brief Get RGB values for a color
 */
static void get_color_rgb(led_shooter_color_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (color == LED_SHOOTER_COLOR_NONE || color >= 3) {
        *r = *g = *b = 0;
        return;
    }
    *r = color_rgb[color][0];
    *g = color_rgb[color][1];
    *b = color_rgb[color][2];
}

/**
 * @brief IRQ handler for button presses
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    led_shooter_game_handle_t game = (led_shooter_game_handle_t)arg;
    int gpio_num = GPIO_NUM_NC;
    
    // Determine which GPIO triggered the interrupt
    uint32_t gpio_status = GPIO.status;
    if (gpio_status & (1ULL << game->button_gpios[LED_SHOOTER_COLOR_RED])) {
        gpio_num = game->button_gpios[LED_SHOOTER_COLOR_RED];
    } else if (gpio_status & (1ULL << game->button_gpios[LED_SHOOTER_COLOR_GREEN])) {
        gpio_num = game->button_gpios[LED_SHOOTER_COLOR_GREEN];
    } else if (gpio_status & (1ULL << game->button_gpios[LED_SHOOTER_COLOR_BLUE])) {
        gpio_num = game->button_gpios[LED_SHOOTER_COLOR_BLUE];
    }
    
    if (gpio_num != GPIO_NUM_NC && game->button_queue != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(game->button_queue, &gpio_num, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * @brief Button task to handle button presses
 */
static void button_task(void *pvParameters)
{
    led_shooter_game_handle_t game = (led_shooter_game_handle_t)pvParameters;
    int gpio_num;
    
    while (game->running) {
        if (xQueueReceive(game->button_queue, &gpio_num, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Debounce delay
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Determine which color was pressed
            led_shooter_color_t color = LED_SHOOTER_COLOR_NONE;
            if (gpio_num == game->button_gpios[LED_SHOOTER_COLOR_RED]) {
                color = LED_SHOOTER_COLOR_RED;
            } else if (gpio_num == game->button_gpios[LED_SHOOTER_COLOR_GREEN]) {
                color = LED_SHOOTER_COLOR_GREEN;
            } else if (gpio_num == game->button_gpios[LED_SHOOTER_COLOR_BLUE]) {
                color = LED_SHOOTER_COLOR_BLUE;
            }
            
            // Add a new shot if there's room
            if (color != LED_SHOOTER_COLOR_NONE && game->running) {
                // Find an empty slot for the shot
                for (size_t i = 0; i < MAX_SHOTS; i++) {
                    if (game->shots[i].position == -1) {
                        game->shots[i].color = color;
                        game->shots[i].position = 0;  // Start from closest LED (position 0)
                        game->active_shots++;
                        ESP_LOGI(TAG, "Shot fired: color %d (active shots: %zu)", color, game->active_shots);
                        break;
                    }
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Main game task
 */
static void game_task(void *pvParameters)
{
    led_shooter_game_handle_t game = (led_shooter_game_handle_t)pvParameters;
    TickType_t last_pattern_move = xTaskGetTickCount();
    TickType_t last_shot_move = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "Game task started");
    
    // Initialize pattern - fill it completely at start
    game->pattern_length = game->pattern_size;
    game->pattern_position = game->led_count - 1;  // Start from furthest LED
    
    // Fill pattern with random colors
    for (size_t i = 0; i < game->pattern_length; i++) {
        game->pattern[i] = (led_shooter_color_t)(esp_random() % 3);
    }
    ESP_LOGI(TAG, "Pattern initialized with %zu colors", game->pattern_length);
    
    // Initialize all shots to inactive
    for (size_t i = 0; i < MAX_SHOTS; i++) {
        game->shots[i].position = -1;
        game->shots[i].color = LED_SHOOTER_COLOR_NONE;
    }
    game->active_shots = 0;
    
    // Clear all LEDs
    led_strip_clear(game->strip);
    
    while (game->running) {
        TickType_t now = xTaskGetTickCount();
        
        // Pattern no longer grows automatically - it only shrinks on matches
        // Initial pattern is filled at game start, then only decreases on successful matches
        
        // Move pattern closer every interval
        if (now - last_pattern_move >= pdMS_TO_TICKS(game->pattern_move_interval_ms)) {
            if (game->pattern_position > 0) {
                game->pattern_position--;
            }
            last_pattern_move = now;
        }
        
        // Move all active shots towards pattern
        if (now - last_shot_move >= pdMS_TO_TICKS(game->shot_speed_ms)) {
            for (size_t i = 0; i < MAX_SHOTS; i++) {
                if (game->shots[i].position >= 0) {
                    game->shots[i].position++;
                    
                    // Check if shot reached the pattern
                    // Pattern fills from furthest to closest, so closest LED is at pattern_position
                    // Pattern array: pattern[0] is furthest, pattern[pattern_length-1] is closest
                    if (game->shots[i].position >= (int)game->pattern_position && game->pattern_length > 0) {
                        // Shot reached the closest LED of the pattern
                        if (game->shots[i].position == (int)game->pattern_position) {
                            // Check if shot matches the closest LED (last in pattern array)
                            size_t closest_pattern_index = game->pattern_length - 1;
                            if (game->shots[i].color == game->pattern[closest_pattern_index]) {
                            // if (1) {
                                // Match! Remove both and make pattern shorter
                                ESP_LOGI(TAG, "Match! Removing closest color, pattern length: %zu -> %zu", 
                                         game->pattern_length, game->pattern_length - 1);
                                
                                // Remove closest LED from pattern
                                game->pattern_length--;
                                
                                // Check if pattern is completely destroyed
                                if (game->pattern_length == 0) {
                                    // Pattern destroyed! Start a new one
                                    ESP_LOGI(TAG, "Pattern destroyed! Starting new pattern");
                                    game->pattern_length = game->pattern_size;
                                    game->pattern_position = game->led_count - 1;  // Reset to furthest LED
                                    
                                    // Fill pattern with new random colors
                                    for (size_t j = 0; j < game->pattern_length; j++) {
                                        game->pattern[j] = (led_shooter_color_t)(esp_random() % 3);
                                    }
                                    ESP_LOGI(TAG, "New pattern initialized with %zu colors", game->pattern_length);
                                } else {
                                    // Move pattern forward (closer) to make it visually shorter
                                    // Only move if pattern still has LEDs and we're not at the end
                                    if (game->pattern_position > 0) {
                                        game->pattern_position--;
                                    }
                                }
                                
                                // Remove shot
                                game->shots[i].position = -1;
                                game->shots[i].color = LED_SHOOTER_COLOR_NONE;
                                game->active_shots--;
                            } else {
                                // No match - shot disappears
                                ESP_LOGI(TAG, "No match. Shot disappears");
                                game->shots[i].position = -1;
                                game->shots[i].color = LED_SHOOTER_COLOR_NONE;
                                game->active_shots--;
                            }
                        } else if (game->shots[i].position > (int)game->pattern_position) {
                            // Shot passed pattern - disappear
                            game->shots[i].position = -1;
                            game->shots[i].color = LED_SHOOTER_COLOR_NONE;
                            game->active_shots--;
                        }
                    } else if (game->shots[i].position >= (int)game->led_count) {
                        // Shot went off the strip
                        game->shots[i].position = -1;
                        game->shots[i].color = LED_SHOOTER_COLOR_NONE;
                        game->active_shots--;
                    }
                }
            }
            last_shot_move = now;
        }
        
        // Update LED display
        led_strip_clear(game->strip);
        
        // Draw pattern (pattern[0] is furthest, pattern[pattern_length-1] is closest)
        for (size_t i = 0; i < game->pattern_length; i++) {
            // Pattern fills from furthest to closest
            // pattern[0] is at pattern_position + (pattern_length - 1)
            // pattern[i] is at pattern_position + (pattern_length - 1 - i)
            size_t led_pos = game->pattern_position + (game->pattern_length - 1 - i);
            if (led_pos < game->led_count) {
                uint8_t r, g, b;
                get_color_rgb(game->pattern[i], &r, &g, &b);
                led_strip_set_pixel(game->strip, led_pos, r, g, b, game->brightness);
            }
        }
        
        // Draw all active shots
        for (size_t i = 0; i < MAX_SHOTS; i++) {
            if (game->shots[i].position >= 0 && game->shots[i].position < (int)game->led_count) {
                uint8_t r, g, b;
                get_color_rgb(game->shots[i].color, &r, &g, &b);
                led_strip_set_pixel(game->strip, game->shots[i].position, r, g, b, game->brightness);
            }
        }
        
        // Refresh display
        led_strip_refresh(game->strip, portMAX_DELAY);
        
        vTaskDelay(pdMS_TO_TICKS(50));  // Small delay to prevent excessive updates
    }
    
    ESP_LOGI(TAG, "Game task stopped");
    vTaskDelete(NULL);
}

esp_err_t led_shooter_game_init(const led_shooter_game_config_t *config, led_shooter_game_handle_t *ret_game)
{
    if (config == NULL || ret_game == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->strip == NULL) {
        ESP_LOGE(TAG, "LED strip handle cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate game structure
    led_shooter_game_handle_t game = (led_shooter_game_handle_t)malloc(sizeof(struct led_shooter_game_t));
    if (game == NULL) {
        ESP_LOGE(TAG, "Failed to allocate game structure");
        return ESP_ERR_NO_MEM;
    }
    memset(game, 0, sizeof(struct led_shooter_game_t));
    
    game->strip = config->strip;
    game->button_gpios[LED_SHOOTER_COLOR_RED] = config->button_red_gpio;
    game->button_gpios[LED_SHOOTER_COLOR_GREEN] = config->button_green_gpio;
    game->button_gpios[LED_SHOOTER_COLOR_BLUE] = config->button_blue_gpio;
    game->led_count = led_strip_get_led_count(config->strip);
    game->pattern_size = config->pattern_size ? config->pattern_size : PATTERN_SIZE_DEFAULT;
    game->pattern_move_interval_ms = config->pattern_move_interval_ms ? config->pattern_move_interval_ms : PATTERN_MOVE_INTERVAL_MS_DEFAULT;
    game->shot_speed_ms = config->shot_speed_ms ? config->shot_speed_ms : SHOT_SPEED_MS_DEFAULT;
    game->brightness = config->brightness ? config->brightness : BRIGHTNESS_DEFAULT;
    game->active_shots = 0;
    game->running = false;
    
    // Create button queue
    game->button_queue = xQueueCreate(10, sizeof(int));
    if (game->button_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button queue");
        free(game);
        return ESP_ERR_NO_MEM;
    }
    
    // Configure GPIO buttons
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Falling edge trigger
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << config->button_red_gpio) | 
                        (1ULL << config->button_green_gpio) | 
                        (1ULL << config->button_blue_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Install GPIO ISR service
    gpio_install_isr_service(0);
    
    // Add ISR handlers for each button (pass game handle to get queue)
    gpio_isr_handler_add(config->button_red_gpio, button_isr_handler, game);
    gpio_isr_handler_add(config->button_green_gpio, button_isr_handler, game);
    gpio_isr_handler_add(config->button_blue_gpio, button_isr_handler, game);
    
    *ret_game = game;
    ESP_LOGI(TAG, "LED shooter game initialized");
    return ESP_OK;
}

esp_err_t led_shooter_game_deinit(led_shooter_game_handle_t game)
{
    if (game == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop game if running
    if (game->running) {
        led_shooter_game_stop(game);
    }
    
    // Remove ISR handlers
    gpio_isr_handler_remove(game->button_gpios[LED_SHOOTER_COLOR_RED]);
    gpio_isr_handler_remove(game->button_gpios[LED_SHOOTER_COLOR_GREEN]);
    gpio_isr_handler_remove(game->button_gpios[LED_SHOOTER_COLOR_BLUE]);
    
    // Delete queue
    if (game->button_queue != NULL) {
        vQueueDelete(game->button_queue);
    }
    
    free(game);
    ESP_LOGI(TAG, "LED shooter game deinitialized");
    return ESP_OK;
}

esp_err_t led_shooter_game_start(led_shooter_game_handle_t game)
{
    if (game == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (game->running) {
        ESP_LOGW(TAG, "Game already running");
        return ESP_OK;
    }
    
    game->running = true;
    
    // Create game task
    BaseType_t ret = xTaskCreate(game_task,
                                "game_task",
                                4096,
                                game,
                                5,
                                &game->game_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create game task");
        game->running = false;
        return ESP_ERR_NO_MEM;
    }
    
    // Create button task
    ret = xTaskCreate(button_task,
                     "button_task",
                     2048,
                     game,
                     6,
                     &game->button_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        vTaskDelete(game->game_task_handle);
        game->running = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Game started");
    return ESP_OK;
}

esp_err_t led_shooter_game_stop(led_shooter_game_handle_t game)
{
    if (game == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!game->running) {
        return ESP_OK;
    }
    
    game->running = false;
    
    // Wait a bit for tasks to finish current operations
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Delete tasks
    if (game->game_task_handle != NULL) {
        vTaskDelete(game->game_task_handle);
        game->game_task_handle = NULL;
    }
    
    if (game->button_task_handle != NULL) {
        vTaskDelete(game->button_task_handle);
        game->button_task_handle = NULL;
    }
    
    // Clear display
    led_strip_clear(game->strip);
    led_strip_refresh(game->strip, portMAX_DELAY);
    
    ESP_LOGI(TAG, "Game stopped");
    return ESP_OK;
}

esp_err_t led_shooter_game_trigger_shot(led_shooter_game_handle_t game, led_shooter_color_t color)
{
    if (game == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (color == LED_SHOOTER_COLOR_NONE || color >= 3) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!game->running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Find an empty slot for the shot
    for (size_t i = 0; i < MAX_SHOTS; i++) {
        if (game->shots[i].position == -1) {
            game->shots[i].color = color;
            game->shots[i].position = 0;  // Start from closest LED (position 0)
            game->active_shots++;
            ESP_LOGI(TAG, "Shot triggered programmatically: color %d (active shots: %zu)", color, game->active_shots);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "No available shot slots");
    return ESP_ERR_NO_MEM;
}

