/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include <stdlib.h>
#include "web_interface.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "led_shooter_game.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_interface";

#define DEFAULT_PORT 80

// HTML page with buttons
static const char html_page[] = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>LED Shooter Game</title>"
"<style>"
"body {"
"  font-family: Arial, sans-serif;"
"  display: flex;"
"  flex-direction: column;"
"  align-items: center;"
"  justify-content: center;"
"  min-height: 100vh;"
"  margin: 0;"
"  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"}"
"h1 {"
"  color: white;"
"  text-shadow: 2px 2px 4px rgba(0,0,0,0.3);"
"  margin-bottom: 40px;"
"}"
".button-container {"
"  display: flex;"
"  gap: 30px;"
"  flex-wrap: wrap;"
"  justify-content: center;"
"}"
".game-button {"
"  width: 150px;"
"  height: 150px;"
"  border: none;"
"  border-radius: 20px;"
"  font-size: 24px;"
"  font-weight: bold;"
"  color: white;"
"  cursor: pointer;"
"  box-shadow: 0 8px 16px rgba(0,0,0,0.3);"
"  transition: all 0.2s;"
"  text-transform: uppercase;"
"}"
".game-button:active {"
"  transform: scale(0.95);"
"  box-shadow: 0 4px 8px rgba(0,0,0,0.3);"
"}"
".button-red {"
"  background: linear-gradient(135deg, #ff6b6b 0%, #ee5a6f 100%);"
"}"
".button-red:hover {"
"  background: linear-gradient(135deg, #ff5252 0%, #e53935 100%);"
"}"
".button-green {"
"  background: linear-gradient(135deg, #51cf66 0%, #40c057 100%);"
"}"
".button-green:hover {"
"  background: linear-gradient(135deg, #40c057 0%, #2f9e44 100%);"
"}"
".button-blue {"
"  background: linear-gradient(135deg, #4dabf7 0%, #339af0 100%);"
"}"
".button-blue:hover {"
"  background: linear-gradient(135deg, #339af0 0%, #228be6 100%);"
"}"
".button-reset {"
"  background: linear-gradient(135deg, #868e96 0%, #495057 100%);"
"  width: 200px;"
"}"
".button-reset:hover {"
"  background: linear-gradient(135deg, #6c757d 0%, #343a40 100%);"
"}"
"</style>"
"</head>"
"<body>"
"<h1>LED Shooter Game</h1>"
"<div class='button-container'>"
"<button class='game-button button-red' onclick='shoot(1)'>Red</button>"
"<button class='game-button button-green' onclick='shoot(0)'>Green</button>"
"<button class='game-button button-blue' onclick='shoot(2)'>Blue</button>"
"<button class='game-button button-reset' onclick='reset()'>Reset</button>"
"</div>"
"<script>"
"function shoot(color) {"
"  fetch('/shoot?color=' + color, { method: 'POST' })"
"    .then(response => response.text())"
"    .then(data => console.log('Shot fired:', data))"
"    .catch(error => console.error('Error:', error));"
"}"
"function reset() {"
"  fetch('/reset', { method: 'POST' })"
"    .then(response => response.text())"
"    .then(data => console.log('Game reset:', data))"
"    .catch(error => console.error('Error:', error));"
"}"
"</script>"
"</body>"
"</html>";

struct web_interface_t {
    httpd_handle_t server;
    led_shooter_game_handle_t game;
    uint16_t port;
};

/**
 * @brief Handler for root path - serves HTML page
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for /shoot endpoint
 */
static esp_err_t shoot_post_handler(httpd_req_t *req)
{
    web_interface_handle_t interface = (web_interface_handle_t)req->user_ctx;
    
    // Get color parameter from query string
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char color_str[8];
        if (httpd_query_key_value(query, "color", color_str, sizeof(color_str)) == ESP_OK) {
            int color = atoi(color_str);
            if (color >= 0 && color < 3) {
                led_shooter_color_t shot_color = (led_shooter_color_t)color;
                esp_err_t ret = led_shooter_game_trigger_shot(interface->game, shot_color);
                if (ret == ESP_OK) {
                    httpd_resp_sendstr(req, "OK");
                    ESP_LOGI(TAG, "Shot triggered via web: color %d", color);
                    return ESP_OK;
                } else {
                    httpd_resp_set_status(req, "500 Internal Server Error");
                    httpd_resp_sendstr(req, "Failed to trigger shot");
                    return ESP_FAIL;
                }
            }
        }
    }
    
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid color parameter");
    return ESP_FAIL;
}

/**
 * @brief Handler for /reset endpoint
 */
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    web_interface_handle_t interface = (web_interface_handle_t)req->user_ctx;
    
    // Reset the game (resets pattern and clears shots without stopping tasks)
    esp_err_t ret = led_shooter_game_reset(interface->game);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to reset game");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "Game reset via web interface");
    return ESP_OK;
}

esp_err_t web_interface_init(const web_interface_config_t *config, web_interface_handle_t *ret_interface)
{
    if (config == NULL || ret_interface == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->game == NULL) {
        ESP_LOGE(TAG, "Game handle cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate web interface structure
    web_interface_handle_t interface = (web_interface_handle_t)malloc(sizeof(struct web_interface_t));
    if (interface == NULL) {
        ESP_LOGE(TAG, "Failed to allocate web interface structure");
        return ESP_ERR_NO_MEM;
    }
    memset(interface, 0, sizeof(struct web_interface_t));
    
    interface->game = config->game;
    interface->port = config->port ? config->port : DEFAULT_PORT;
    
    // Configure HTTP server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = interface->port;
    server_config.max_uri_handlers = 10;
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&interface->server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        free(interface);
        return ret;
    }
    
    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = interface
    };
    httpd_register_uri_handler(interface->server, &root_uri);
    
    httpd_uri_t shoot_uri = {
        .uri = "/shoot",
        .method = HTTP_POST,
        .handler = shoot_post_handler,
        .user_ctx = interface
    };
    httpd_register_uri_handler(interface->server, &shoot_uri);
    
    httpd_uri_t reset_uri = {
        .uri = "/reset",
        .method = HTTP_POST,
        .handler = reset_post_handler,
        .user_ctx = interface
    };
    httpd_register_uri_handler(interface->server, &reset_uri);
    
    *ret_interface = interface;
    ESP_LOGI(TAG, "Web interface started on port %d", interface->port);
    return ESP_OK;
}

esp_err_t web_interface_deinit(web_interface_handle_t interface)
{
    if (interface == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop HTTP server
    if (interface->server != NULL) {
        httpd_stop(interface->server);
    }
    
    free(interface);
    ESP_LOGI(TAG, "Web interface deinitialized");
    return ESP_OK;
}

