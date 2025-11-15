idf_component_register(SRCS "main.c"
                       REQUIRES led_strip led_shooter_game web_interface esp_wifi nvs_flash
                       INCLUDE_DIRS ".")
