FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       PRIV_REQUIRES esp_adc esp_driver_gpio log freertos)