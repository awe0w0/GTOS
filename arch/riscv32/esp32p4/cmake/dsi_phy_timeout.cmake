# Build the v6.1 DSI bus driver with bounded PHY startup in the A3 build tree.
idf_component_get_property(lcd_dir esp_lcd COMPONENT_DIR)
idf_component_get_property(lcd_target esp_lcd COMPONENT_LIB)
set(dsi_dir "${lcd_dir}/dsi")
set(dsi_original "${dsi_dir}/esp_lcd_mipi_dsi_bus.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dsi_original}")
file(READ "${dsi_original}" dsi_source)
string(REPLACE "\r\n" "\n" dsi_source "${dsi_source}")

set(original_wait [=[
    while (!mipi_dsi_phy_ll_is_pll_locked(hal->host)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    while (!mipi_dsi_phy_ll_are_lanes_stopped(hal->host, num_data_lanes)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
]=])
set(bounded_wait [=[
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(250) + 1;
    while (!mipi_dsi_phy_ll_is_pll_locked(hal->host)) {
        ESP_GOTO_ON_FALSE((TickType_t)(xTaskGetTickCount() - started) < timeout,
                          ESP_ERR_TIMEOUT, err, TAG, "D-PHY PLL lock timed out");
        vTaskDelay(1);
    }
    while (!mipi_dsi_phy_ll_are_lanes_stopped(hal->host, num_data_lanes)) {
        ESP_GOTO_ON_FALSE((TickType_t)(xTaskGetTickCount() - started) < timeout,
                          ESP_ERR_TIMEOUT, err, TAG, "D-PHY lane idle timed out");
        vTaskDelay(1);
    }
]=])
set(original_cleanup [=[
    if (dsi_bus) {
        esp_lcd_del_dsi_bus(dsi_bus);
    }
]=])
set(phy_cleanup [=[
    if (dsi_bus) {
        if (dsi_bus->hal.host) {
            mipi_dsi_hal_deinit(&dsi_bus->hal);
        }
        esp_lcd_del_dsi_bus(dsi_bus);
    }
]=])

string(FIND "${dsi_source}" "${original_wait}" wait_position)
string(FIND "${dsi_source}" "${original_cleanup}" cleanup_position)
get_target_property(lcd_sources "${lcd_target}" SOURCES)
list(FIND lcd_sources "${dsi_original}" source_position)
if(wait_position EQUAL -1 OR cleanup_position EQUAL -1 OR source_position EQUAL -1)
    message(FATAL_ERROR "A3 DSI PHY timeout requires the v6.1 ESP-IDF DSI bus driver layout")
endif()

string(REPLACE "${original_wait}" "${bounded_wait}" dsi_source "${dsi_source}")
string(REPLACE "${original_cleanup}" "${phy_cleanup}" dsi_source "${dsi_source}")
set(dsi_bounded "${CMAKE_CURRENT_BINARY_DIR}/gtos/dsi/esp_lcd_mipi_dsi_bus.c")
file(CONFIGURE OUTPUT "${dsi_bounded}" CONTENT "${dsi_source}" @ONLY NEWLINE_STYLE UNIX)

list(REMOVE_AT lcd_sources ${source_position})
list(APPEND lcd_sources "${dsi_bounded}")
set_property(TARGET "${lcd_target}" PROPERTY SOURCES "${lcd_sources}")
target_include_directories("${lcd_target}" PRIVATE "${dsi_dir}")
