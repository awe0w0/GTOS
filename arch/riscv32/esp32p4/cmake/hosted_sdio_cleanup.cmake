# Apply the A3 SDIO cleanup fixes to the pinned ESP-Hosted 3.1.0 sources.
function(a3_hosted_read_source target relative result)
    get_target_property(source_dir "${target}" SOURCE_DIR)
    set(original "${source_dir}/${relative}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    set(${result} "${source}" PARENT_SCOPE)
endfunction()

function(a3_hosted_replace source original replacement)
    string(FIND "${${source}}" "${original}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Unexpected ESP-Hosted 3.1.0 SDIO source layout in ${source}")
    endif()
    string(REPLACE "${original}" "${replacement}" updated "${${source}}")
    set(${source} "${updated}" PARENT_SCOPE)
endfunction()

function(a3_hosted_use_source target relative source)
    get_target_property(sources "${target}" SOURCES)
    list(FIND sources "${relative}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing ${relative} in ${target}")
    endif()
    get_filename_component(filename "${relative}" NAME)
    set(updated "${CMAKE_CURRENT_BINARY_DIR}/gtos/hosted/${filename}")
    file(CONFIGURE OUTPUT "${updated}" CONTENT "${source}" @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_AT sources ${position})
    list(APPEND sources "${updated}")
    set_property(TARGET "${target}" PROPERTY SOURCES "${sources}")
endfunction()

set(bus_source_path src/eh_host_bus_sdio.c)
a3_hosted_read_source(eh_host_mcu_transport "${bus_source_path}" bus_source)
a3_hosted_replace(bus_source [=[static void *sdio_handle = NULL;]=]
    [=[static void *sdio_handle = NULL;
static bool sdio_card_ready;]=])
a3_hosted_replace(bus_source [=[while (!sdio_start_write_thread)]=]
    [=[while (s_running && !sdio_start_write_thread)]=])
a3_hosted_replace(bus_source [=[
	} while (i < num_loops);

	return res;
]=] [=[
	} while (i < num_loops);

	sdio_card_ready = res == ESP_OK;
	return res;
]=])
a3_hosted_replace(bus_source [=[
	if (sdio_handle) {
		SDIO_DRV_LOCK();
		sdio_generate_slave_intr(ESP_CLOSE_DATA_PATH);
]=] [=[
	if (sdio_card_ready) {
		SDIO_DRV_LOCK();
		sdio_generate_slave_intr(ESP_CLOSE_DATA_PATH);
]=])
a3_hosted_replace(bus_source [=[
		sdio_handle = NULL;
	}
	eh_frame_deinit();
]=] [=[
		sdio_handle = NULL;
		sdio_card_ready = false;
	}
	eh_frame_deinit();
]=])
a3_hosted_use_source(eh_host_mcu_transport "${bus_source_path}" "${bus_source}")

# Deinit can run before enumeration. A failed attempt must also release any
# DMA buffer before IDF clears the card structure on the next attempt.
set(port_source_path src/eh_host_port_sdio.c)
a3_hosted_read_source(eh_host_port_idf "${port_source_path}" port_source)
a3_hosted_replace(port_source "s_sdmmc_context.card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));"
    "s_sdmmc_context.card = calloc(1, sizeof(sdmmc_card_t));")
a3_hosted_replace(port_source [=[
fail:
	return ESP_FAIL;
}

int eh_host_port_sdio_card_deinit(void *ctx)
]=] [=[
fail:
	eh_host_port_sdio_card_deinit(ctx);
	return ESP_FAIL;
}

int eh_host_port_sdio_card_deinit(void *ctx)
]=])
a3_hosted_use_source(eh_host_port_idf "${port_source_path}" "${port_source}")
