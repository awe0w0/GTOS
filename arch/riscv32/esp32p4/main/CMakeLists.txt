get_filename_component(gtos_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(dashboard "${gtos_root}/apps/a3_dashboard")
set(third_party "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")

idf_component_register(
    SRCS "boot.c" "kernel.cpp" "board_i2c.c" "board_input.c" "board_storage.c" "board_display.c"
         "board_radio.c" "board_panel.c"
         "${dashboard}/dashboard.c" "${dashboard}/font.cpp"
         "${third_party}/lsm6ds3tr-c/lsm6ds3tr-c_reg.c"
         "${third_party}/bq25601/bq25601.c"
         "${third_party}/st7703/esp_lcd_st7703.c"
         "${gtos_root}/src/common/boot_log.cpp"
         "${gtos_root}/src/memorymanagement.cpp"
         "${gtos_root}/src/apps/package.cpp"
         "${gtos_root}/src/apps/vm.cpp"
    PRIV_INCLUDE_DIRS "." "${gtos_root}/include" "${gtos_root}/src/gui" "${dashboard}"
                      "${third_party}/lsm6ds3tr-c" "${third_party}/bq25601" "${third_party}/st7703"
    PRIV_REQUIRES esp_psram heap esp_timer nvs_flash esp_driver_gpio esp_driver_i2c
                  esp_driver_ledc esp_driver_pcnt esp_driver_uart esp_driver_usb_serial_jtag
                  esp_event esp_netif esp_wifi bt esp_lcd esp_driver_ppa
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GTOS_EXTERNAL_NEW_DELETE)
target_link_options(${COMPONENT_LIB} INTERFACE "LINKER:--wrap=eh_host_port_reset_slave")
set_source_files_properties("${third_party}/st7703/esp_lcd_st7703.c" PROPERTIES
    COMPILE_DEFINITIONS "ESP_LCD_ST7703_VER_MAJOR=1;ESP_LCD_ST7703_VER_MINOR=0;ESP_LCD_ST7703_VER_PATCH=4")
if(GTOS_A3_PANEL_PROFILE)
    target_sources(${COMPONENT_LIB} PRIVATE "${GTOS_A3_PANEL_PROFILE}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GTOS_A3_PANEL_PROFILE)
endif()

idf_build_get_property(python PYTHON)
set(catch_package "${CMAKE_CURRENT_BINARY_DIR}/catch.gtapp")
add_custom_command(OUTPUT "${catch_package}"
    COMMAND "${python}" "${gtos_root}/tools/package.py" build
            "${gtos_root}/apps/catch.json" "${catch_package}"
    DEPENDS "${gtos_root}/tools/package.py" "${gtos_root}/apps/catch.json"
    VERBATIM
)
target_add_binary_data(${COMPONENT_LIB} "${catch_package}" BINARY)
