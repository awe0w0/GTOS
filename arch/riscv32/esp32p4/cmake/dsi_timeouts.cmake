# Build the pinned ESP-IDF DSI drivers with bounded waits in the A3 build tree.
idf_component_get_property(lcd_dir esp_lcd COMPONENT_DIR)
idf_component_get_property(lcd_target esp_lcd COMPONENT_LIB)
set(dsi_dir "${lcd_dir}/dsi")
target_include_directories("${lcd_target}" PRIVATE "${dsi_dir}" "${CMAKE_CURRENT_LIST_DIR}")

function(a3_dsi_read_source filename result)
    set(original "${dsi_dir}/${filename}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${result} "${source}" PARENT_SCOPE)
endfunction()

function(a3_dsi_replace source original replacement)
    string(FIND "${${source}}" "${original}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Unexpected v6.1 ESP-IDF driver layout while updating ${source}")
    endif()
    string(REPLACE "${original}" "${replacement}" updated "${${source}}")
    set(${source} "${updated}" PARENT_SCOPE)
endfunction()

function(a3_dsi_use_source filename source)
    get_target_property(lcd_sources "${lcd_target}" SOURCES)
    list(FIND lcd_sources "${dsi_dir}/${filename}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing ${filename} in the v6.1 ESP-IDF DSI component")
    endif()
    set(updated "${CMAKE_CURRENT_BINARY_DIR}/gtos/dsi/${filename}")
    file(CONFIGURE OUTPUT "${updated}" CONTENT "${source}" @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_AT lcd_sources ${position})
    list(APPEND lcd_sources "${updated}")
    set_property(TARGET "${lcd_target}" PROPERTY SOURCES "${lcd_sources}")
endfunction()

a3_dsi_read_source(esp_lcd_mipi_dsi_bus.c bus_source)
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
    int bus_id = bus->bus_id;
    // disable the clock source for DSI PHY
]=])
set(phy_cleanup [=[
    int bus_id = bus->bus_id;
    if (bus->hal.host) {
        mipi_dsi_hal_deinit(&bus->hal);
    }
    // disable the clock source for DSI PHY
]=])
a3_dsi_replace(bus_source "${original_wait}" "${bounded_wait}")
a3_dsi_replace(bus_source "${original_cleanup}" "${phy_cleanup}")
a3_dsi_use_source(esp_lcd_mipi_dsi_bus.c "${bus_source}")

a3_dsi_read_source(esp_lcd_panel_io_dbi.c dbi_source)
a3_dsi_replace(dbi_source [=[#include "mipi_dsi_priv.h"]=]
    [=[#include "mipi_dsi_priv.h"
#include "dsi_dbi_timeout.h"]=])
set(original_tx [=[
    mipi_dsi_hal_host_gen_write_dcs_command(hal, dbi_io->virtual_channel, lcd_cmd, dbi_io->lcd_cmd_bits / 8, param, param_size);

    return ESP_OK;
]=])
set(original_rx [=[
    mipi_dsi_hal_host_gen_read_dcs_command(hal, dbi_io->virtual_channel, lcd_cmd, dbi_io->lcd_cmd_bits / 8, param, param_size);

    return ESP_OK;
]=])
a3_dsi_replace(dbi_source "${original_tx}" [=[
    return a3_dbi_write(hal->host, dbi_io->virtual_channel, lcd_cmd, dbi_io->lcd_cmd_bits / 8, param, param_size);
]=])
a3_dsi_replace(dbi_source "${original_rx}" [=[
    return a3_dbi_read(hal->host, dbi_io->virtual_channel, lcd_cmd, dbi_io->lcd_cmd_bits / 8, param, param_size);
]=])
a3_dsi_use_source(esp_lcd_panel_io_dbi.c "${dbi_source}")
