#include "board_a3.h"
#include "board_panel.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

enum { FrameBytes = A3_WIDTH * A3_HEIGHT * sizeof(uint16_t) };

static esp_ldo_channel_handle_t phy;
static esp_lcd_dsi_bus_handle_t bus;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;
static uint16_t* staging;
static ppa_client_handle_t rotation;
static SemaphoreHandle_t transfer_done;
static bool transfer_pending;
static const A3PanelProfile* active;

const A3PanelProfile* a3_panel_profile(void) {
#ifdef GTOS_A3_PANEL_PROFILE
    const A3PanelProfile* profile = &gtos_a3_panel_profile;
    const unsigned w = profile->timing.h_size, h = profile->timing.v_size;
    if (!((w == A3_WIDTH && h == A3_HEIGHT) || (w == A3_HEIGHT && h == A3_WIDTH))
        || !profile->commands || !profile->command_count
        || !(profile->pixel_clock_mhz > 0) || !(profile->lane_rate_mbps > 0)
        || !profile->power_on_ms || !profile->reset_release_ms
        || !profile->backlight_pwm_hz || !profile->backlight_max_percent
        || profile->backlight_max_percent > 100
        || (profile->wire_format != LCD_COLOR_FMT_RGB565
            && profile->wire_format != LCD_COLOR_FMT_RGB888)) return NULL;
    return profile;
#else
    return NULL;
#endif
}

static bool retire_transfer(TickType_t wait) {
    if (!transfer_pending) return true;
    if (xSemaphoreTake(transfer_done, wait) != pdTRUE) return false;
    transfer_pending = false;
    return true;
}

bool a3_panel_stop(void) {
    if (!retire_transfer(pdMS_TO_TICKS(500))) return false;
    if (panel) {
        if (active) esp_lcd_panel_disp_on_off(panel, false);
        esp_lcd_panel_del(panel);
        panel = NULL;
    }
    if (io) {
        esp_lcd_panel_io_del(io);
        io = NULL;
    }
    if (bus) {
        esp_lcd_del_dsi_bus(bus);
        bus = NULL;
    }
    if (phy) {
        esp_ldo_release_channel(phy);
        phy = NULL;
    }
    if (transfer_done) {
        vSemaphoreDelete(transfer_done);
        transfer_done = NULL;
    }
    if (rotation) {
        ppa_unregister_client(rotation);
        rotation = NULL;
    }
    heap_caps_free(staging);
    staging = NULL;
    active = NULL;
    return true;
}

static bool frame_copied(esp_lcd_panel_handle_t display, esp_lcd_dpi_panel_event_data_t* event,
                         void* semaphore) {
    (void)display;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(semaphore, &wake);
    return wake == pdTRUE;
}

static bool enable_acceleration(bool portrait) {
    if (portrait) {
        const ppa_client_config_t ppa = {.oper_type = PPA_OPERATION_SRM};
        if (ppa_register_client(&ppa, &rotation) != ESP_OK) return false;
    }
    transfer_done = xSemaphoreCreateBinary();
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_color_trans_done = frame_copied};
    return transfer_done
        && esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, transfer_done) == ESP_OK
        && esp_lcd_dpi_panel_enable_dma2d(panel) == ESP_OK;
}

bool a3_panel_start(const A3PanelProfile* profile) {
    if (!profile || active) return false;
    const bool portrait = profile->timing.h_size == A3_HEIGHT;
    if (portrait || profile->use_graphics_acceleration) {
        staging = heap_caps_aligned_alloc(128, FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!staging) return false;
    }
    const esp_ldo_channel_config_t ldo = {.chan_id = 3, .voltage_mv = 2500};
    const esp_lcd_dsi_bus_config_t dsi = {
        .bus_id = 0, .num_data_lanes = 2, .lane_bit_rate_mbps = profile->lane_rate_mbps,
    };
    const esp_lcd_dbi_io_config_t dbi = {.lcd_cmd_bits = 8, .lcd_param_bits = 8};
    if (esp_ldo_acquire_channel(&ldo, &phy) != ESP_OK
        || esp_lcd_new_dsi_bus(&dsi, &bus) != ESP_OK
        || esp_lcd_new_panel_io_dbi(bus, &dbi, &io) != ESP_OK) {
        a3_panel_stop();
        return false;
    }
    const esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clock_freq_mhz = profile->pixel_clock_mhz,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = profile->wire_format, .num_fbs = 1, .video_timing = profile->timing,
    };
    st7703_vendor_config_t vendor = {
        .init_cmds = profile->commands, .init_cmds_size = profile->command_count,
        .init_in_command_mode = profile->init_in_command_mode,
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi},
    };
    const esp_lcd_panel_dev_config_t device = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = profile->wire_format == LCD_COLOR_FMT_RGB565 ? 16 : 24,
        .vendor_config = &vendor,
    };
    if (esp_lcd_new_panel_st7703(io, &device, &panel) != ESP_OK
        || esp_lcd_panel_init(panel) != ESP_OK
        || (profile->use_graphics_acceleration && !enable_acceleration(portrait))
        || esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
        a3_panel_stop();
        return false;
    }
    active = profile;
    return true;
}

static bool rotate_frame(const uint16_t* pixels) {
    if (rotation) {
        const ppa_srm_oper_config_t operation = {
            .in = {.buffer = pixels, .pic_w = A3_WIDTH, .pic_h = A3_HEIGHT,
                   .block_w = A3_WIDTH, .block_h = A3_HEIGHT, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
            .out = {.buffer = staging, .buffer_size = FrameBytes, .pic_w = A3_HEIGHT,
                    .pic_h = A3_WIDTH, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
            .rotation_angle = active->portrait_clockwise
                ? PPA_SRM_ROTATION_ANGLE_270 : PPA_SRM_ROTATION_ANGLE_90,
            .scale_x = 1, .scale_y = 1, .mode = PPA_TRANS_MODE_BLOCKING,
        };
        return ppa_do_scale_rotate_mirror(rotation, &operation) == ESP_OK;
    }
    for (unsigned y = 0; y < A3_HEIGHT; ++y) {
        for (unsigned x = 0; x < A3_WIDTH; ++x) {
            size_t destination = active->portrait_clockwise
                ? x * A3_HEIGHT + A3_HEIGHT - 1 - y : (A3_WIDTH - 1 - x) * A3_HEIGHT + y;
            staging[destination] = pixels[y * A3_WIDTH + x];
        }
    }
    return true;
}

bool a3_panel_present(const uint16_t* pixels) {
    if (!active || !retire_transfer(0)) return false;
    if (active->timing.h_size == A3_HEIGHT) {
        if (!rotate_frame(pixels)) return false;
        pixels = staging;
    } else if (staging) {
        memcpy(staging, pixels, FrameBytes);
        pixels = staging;
    }
    if (esp_lcd_panel_draw_bitmap(panel, 0, 0, active->timing.h_size, active->timing.v_size,
                                  pixels) != ESP_OK) return false;
    // An asynchronous DMA2D operation owns staging until its callback.
    transfer_pending = transfer_done != NULL;
    return true;
}
