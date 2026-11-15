# Qualify fresh DMA2D channels before the pinned SDK exposes them to a client.
idf_component_get_property(a3_dma_dir esp_driver_dma COMPONENT_DIR)
idf_component_get_property(a3_dma_target esp_driver_dma COMPONENT_LIB)
set(a3_dma_file "${a3_dma_dir}/src/dma2d.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${a3_dma_file}")
file(READ "${a3_dma_file}" a3_dma_source)
string(REPLACE "\r\n" "\n" a3_dma_source "${a3_dma_source}")
set(a3_dma_original [=[
            s_platform.groups[group_id] = pre_alloc_group; // register to platform

            // Enable bus clock for the 2D-DMA registers
            PERIPH_RCC_ATOMIC() {
                dma2d_ll_enable_bus_clock(group_id, true);
                dma2d_ll_reset_register(group_id);
            }
            dma2d_hal_init(&pre_alloc_group->hal, group_id); // initialize HAL context
            // Enable 2D-DMA module clock
            dma2d_ll_hw_enable(s_platform.groups[group_id]->hal.dev, true);
]=])
set(a3_dma_checked [=[
            PERIPH_RCC_ATOMIC() {
                dma2d_ll_enable_bus_clock(group_id, true);
                dma2d_ll_reset_register(group_id);
            }
            dma2d_hal_init(&pre_alloc_group->hal, group_id);
            dma2d_ll_hw_enable(pre_alloc_group->hal.dev, true);
            if (!a3_dma2d_reset_ready(pre_alloc_group->hal.dev)) {
                dma2d_ll_hw_enable(pre_alloc_group->hal.dev, false);
                PERIPH_RCC_ATOMIC() {
                    dma2d_ll_enable_bus_clock(group_id, false);
                }
                free(pre_alloc_tx_channels);
                free(pre_alloc_rx_channels);
                free(pre_alloc_group);
                *ret_pool = NULL;
                _lock_release(&s_platform.mutex);
                return ESP_ERR_TIMEOUT;
            }
            s_platform.groups[group_id] = pre_alloc_group;
]=])
string(FIND "${a3_dma_source}" "${a3_dma_original}" a3_dma_init)
get_target_property(a3_dma_sources "${a3_dma_target}" SOURCES)
list(FIND a3_dma_sources "${a3_dma_file}" a3_dma_index)
if(a3_dma_init EQUAL -1 OR a3_dma_index EQUAL -1)
    message(FATAL_ERROR "Unexpected v6.1 ESP-IDF DMA2D driver layout")
endif()
string(REPLACE "${a3_dma_original}" "${a3_dma_checked}" a3_dma_source "${a3_dma_source}")
set(a3_dma_updated "${CMAKE_CURRENT_BINARY_DIR}/gtos/dma2d/dma2d.c")
file(CONFIGURE OUTPUT "${a3_dma_updated}" CONTENT "#include \"dma2d_readiness.h\"\n${a3_dma_source}"
    @ONLY NEWLINE_STYLE UNIX)
list(REMOVE_AT a3_dma_sources ${a3_dma_index})
list(APPEND a3_dma_sources "${a3_dma_updated}")
set_property(TARGET "${a3_dma_target}" PROPERTY SOURCES "${a3_dma_sources}")
target_include_directories("${a3_dma_target}" PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
