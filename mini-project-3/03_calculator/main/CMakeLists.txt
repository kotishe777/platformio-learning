idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "../../common"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_pcnt log freertos)
