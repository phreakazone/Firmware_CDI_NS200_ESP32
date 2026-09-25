idf_component_register(
    SRCS
        "main.c"
        "cdi_board_esp32.c"
        "cdi_module_io.c"
        "cdi_timebase.c"
        "cdi_engine_esp32.c"
        "cdi_ble_nimble.c"
        "cdi_r5.c"
        "cdi_timing_modes.c"
        "cdi_r5_charger.c"
        "cdi_r5_protocol.c"
        "cdi_r5_ble.c"
        "cdi_r8_oem_learn.c"
        "cdi_r8_ota.c"
        "cdi_selftest.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2c esp_adc esp_timer nvs_flash esp_pm bt esp_driver_gptimer esp_driver_mcpwm esp_driver_uart app_update
)

# Menekan peringatan perbedaan nama section IRAM antara deklarasi dan definisi fungsi
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=attributes")