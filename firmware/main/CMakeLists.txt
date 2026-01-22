idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES lib nvs_flash esp_http_server esp_wifi)