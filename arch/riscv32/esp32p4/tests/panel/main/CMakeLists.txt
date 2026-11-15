get_filename_component(a3 "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(gtos_root "${a3}/../../.." ABSOLUTE)
set(st7703 "${a3}/third_party/st7703")

idf_component_register(
    SRCS "panel_test.c" "../profile.c" "${a3}/main/board_display.c"
         "${a3}/main/board_panel.c" "${st7703}/esp_lcd_st7703.c"
    PRIV_INCLUDE_DIRS "${a3}/main" "${gtos_root}/include" "${st7703}"
                      "$ENV{IDF_PATH}/components/esp_lcd/dsi"
    PRIV_REQUIRES esp_psram heap esp_timer esp_driver_gpio esp_driver_ledc esp_hw_support
                  esp_lcd esp_driver_ppa esp_pm
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GTOS_A3_PANEL_PROFILE
    ESP_LCD_ST7703_VER_MAJOR=1 ESP_LCD_ST7703_VER_MINOR=0 ESP_LCD_ST7703_VER_PATCH=4)
if(GTOS_A3_TEST_BACKLIGHT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GTOS_A3_TEST_BACKLIGHT)
endif()
foreach(symbol esp_ldo_acquire_channel esp_ldo_release_channel
        esp_lcd_new_dsi_bus esp_lcd_del_dsi_bus esp_lcd_new_panel_io_dbi esp_lcd_new_panel_dpi
        esp_lcd_dpi_panel_register_event_callbacks esp_lcd_dpi_panel_enable_dma2d esp_lcd_dpi_panel_disable_dma2d
        ppa_register_client ppa_unregister_client ppa_do_scale_rotate_mirror)
    target_link_options(${COMPONENT_LIB} INTERFACE "LINKER:--wrap=${symbol}")
endforeach()
