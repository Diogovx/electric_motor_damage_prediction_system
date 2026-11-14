file(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES esp_timer esp_driver_i2c esp_driver_gpio esp_partition esp_rom
             esp_wifi esp_event esp_netif nvs_flash edge-impulse
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format=
    -Wno-error=unused-but-set-parameter
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-variable
    -Wno-error=unused-function
    -Wno-error=missing-field-initializers
    -Wno-error=deprecated-declarations
    -Wno-error=maybe-uninitialized
    -Wno-error=sign-compare
)
