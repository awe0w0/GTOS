#include "board_a3.h"
#include "board_panel.h"
#include "driver/ppa.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

enum { FrameBytes = A3_WIDTH * A3_HEIGHT * sizeof(uint16_t), RotationWaitMs = 100 };

static esp_ldo_channel_handle_t phy;
static esp_lcd_dsi_bus_handle_t bus;
static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;
static uint16_t* staging;
static struct {
    ppa_client_handle_t client;
    uint16_t* input;
    uint16_t* output;
    SemaphoreHandle_t done;
    bool pending;
} rotation;
static SemaphoreHandle_t transfer_done, vsync;
static bool transfer_pending, dma2d_enabled;
static const A3PanelProfile* active;

const A3PanelProfile* a3_panel_profile(void) {
#ifdef GTOS_A3_PANEL_PROFILE
    const A3PanelProfile* profile = &gtos_a3_panel_profile;
    const unsigned w = profile->timing.h_size, h = profile->timing.v_size;
    if (!((w == A3_WIDTH && h == A3_HEIGHT) || (w == A3_HEIGHT && h == A3_WIDTH))
        || !profile->commands || !profile->command_count
        || !(profile->pixel_clock_mhz > 0) || !isfinite(profile->pixel_clock_mhz)
        || !(profile->lane_rate_mbps > 0) || !isfinite(profile->lane_rate_mbps)
        || !profile->power_on_ms || !profile->reset_release_ms
        || (profile->backlight_max_percent && !profile->backlight_pwm_hz)
        || profile->backlight_max_percent > 100
        || (profile->wire_format != LCD_COLOR_FMT_RGB565
            && profile->wire_format != LCD_COLOR_FMT_RGB888)) return NULL;
    for (uint16_t i = 0; i < profile->command_count; ++i)
        if (profile->commands[i].data_bytes && !profile->commands[i].data) return NULL;
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

static bool retire_rotation(TickType_t wait) {
    if (!rotation.pending) return true;
    if (xSemaphoreTake(rotation.done, wait) != pdTRUE) return false;
    rotation.pending = false;
    return true;
}

static void release_idle_rotation(void) {
    // PPA may still access its own buffers after the display has powered down.
    if (!retire_rotation(0)) return;
    if (rotation.client && ppa_unregister_client(rotation.client) != ESP_OK) return;
    if (rotation.done) vSemaphoreDelete(rotation.done);
    heap_caps_free(rotation.input);
    heap_caps_free(rotation.output);
    memset(&rotation, 0, sizeof(rotation));
}

static bool enable_dma2d(void) {
    if (dma2d_enabled) return true;
    dma2d_enabled = esp_lcd_dpi_panel_enable_dma2d(panel) == ESP_OK;
    return dma2d_enabled;
}

static bool disable_dma2d(void) {
    if (!dma2d_enabled) return true;
    if (esp_lcd_dpi_panel_disable_dma2d(panel) != ESP_OK) return false;
    dma2d_enabled = false;
    return true;
}

bool a3_panel_stop(void) {
    if (!retire_transfer(pdMS_TO_TICKS(500)) || !disable_dma2d()) return false;
    if (panel) {
        if (active) esp_lcd_panel_disp_on_off(panel, false);
        active = NULL;
        if (esp_lcd_panel_del(panel) != ESP_OK) return false;
        panel = NULL;
    }
    if (io) {
        if (esp_lcd_panel_io_del(io) != ESP_OK) return false;
        io = NULL;
    }
    if (bus) {
        if (esp_lcd_del_dsi_bus(bus) != ESP_OK) return false;
        bus = NULL;
    }
    if (phy) {
        if (esp_ldo_release_channel(phy) != ESP_OK) return false;
        phy = NULL;
    }
    if (transfer_done) {
        vSemaphoreDelete(transfer_done);
        transfer_done = NULL;
    }
    if (vsync) {
        vSemaphoreDelete(vsync);
        vsync = NULL;
    }
    release_idle_rotation();
    heap_caps_free(staging);
    staging = NULL;
    active = NULL;
    return true;
}

static bool IRAM_ATTR frame_copied(esp_lcd_panel_handle_t display, esp_lcd_dpi_panel_event_data_t* event,
                                   void* semaphore) {
    (void)display;
    (void)event;
    // Without the DMA draw hook, ESP-IDF calls this from the synchronous CPU copy.
    if (!dma2d_enabled) return false;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(semaphore, &wake);
    return wake == pdTRUE;
}

static bool IRAM_ATTR frame_sync(esp_lcd_panel_handle_t display, esp_lcd_dpi_panel_event_data_t* event,
                                 void* unused) {
    (void)display;
    (void)event;
    (void)unused;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(vsync, &wake);
    return wake == pdTRUE;
}

static bool IRAM_ATTR frame_rotated(ppa_client_handle_t client, ppa_event_data_t* event, void* semaphore) {
    (void)client;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(semaphore, &wake);
    return wake == pdTRUE;
}

static uint16_t* allocate_frame(void) {
    return heap_caps_aligned_alloc(128, FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool reserve_staging(void) {
    if (!staging) staging = allocate_frame();
    return staging != NULL;
}

static bool configure_rotation(void) {
    if (rotation.client) return rotation.done != NULL;
    rotation.input = allocate_frame();
    rotation.output = allocate_frame();
    const ppa_client_config_t config = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    const ppa_event_callbacks_t callbacks = {.on_trans_done = frame_rotated};
    if (!rotation.input || !rotation.output
        || ppa_register_client(&config, &rotation.client) != ESP_OK
        || ppa_client_register_event_callbacks(rotation.client, &callbacks) != ESP_OK) return false;
    rotation.done = xSemaphoreCreateBinary();
    return rotation.done != NULL;
}

static bool configure_frame_events(const A3PanelProfile* profile, bool portrait) {
    const bool accelerate = profile->use_graphics_acceleration;
    if (accelerate && portrait && !configure_rotation()) return false;
    if (accelerate) {
        transfer_done = xSemaphoreCreateBinary();
        if (!transfer_done) return false;
    }
    if (profile->backlight_max_percent) {
        vsync = xSemaphoreCreateBinary();
        if (!vsync) return false;
    }
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = transfer_done ? frame_copied : NULL,
        .on_vsync = vsync ? frame_sync : NULL,
    };
    if ((transfer_done || vsync)
        && esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, transfer_done) != ESP_OK) return false;
    // Portrait drawing may need to bypass the DMA2D pool when PPA is still pending.
    return !accelerate || portrait || enable_dma2d();
}

bool a3_panel_start(const A3PanelProfile* profile) {
    if (!profile || panel || io || bus || phy || transfer_done || vsync || staging) return false;
    release_idle_rotation();
    const bool portrait = profile->timing.h_size == A3_HEIGHT;
    const bool needs_staging = portrait ? !profile->use_graphics_acceleration : profile->use_graphics_acceleration;
    if (needs_staging && !reserve_staging()) return false;
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
        || !configure_frame_events(profile, portrait)
        || esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
        a3_panel_stop();
        return false;
    }
    active = profile;
    return true;
}

static bool rotate_with_ppa(const uint16_t* pixels) {
    if (!retire_rotation(0)) return false;
    // The caller may redraw or release pixels even if this PPA transaction times out.
    memcpy(rotation.input, pixels, FrameBytes);
    const ppa_srm_oper_config_t operation = {
        .in = {.buffer = rotation.input, .pic_w = A3_WIDTH, .pic_h = A3_HEIGHT,
               .block_w = A3_WIDTH, .block_h = A3_HEIGHT, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .out = {.buffer = rotation.output, .buffer_size = FrameBytes, .pic_w = A3_HEIGHT,
                .pic_h = A3_WIDTH, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle = active->portrait_clockwise
            ? PPA_SRM_ROTATION_ANGLE_270 : PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 1, .scale_y = 1, .mode = PPA_TRANS_MODE_NON_BLOCKING, .user_data = rotation.done,
    };
    if (ppa_do_scale_rotate_mirror(rotation.client, &operation) != ESP_OK) return false;
    rotation.pending = true;
    if (retire_rotation(pdMS_TO_TICKS(RotationWaitMs) + 1)) return true;
    ESP_LOGW("a3_panel", "PPA rotation timed out");
    return false;
}

static void rotate_frame(const uint16_t* pixels) {
    for (unsigned y = 0; y < A3_HEIGHT; ++y) {
        for (unsigned x = 0; x < A3_WIDTH; ++x) {
            size_t destination = active->portrait_clockwise
                ? x * A3_HEIGHT + A3_HEIGHT - 1 - y : (A3_WIDTH - 1 - x) * A3_HEIGHT + y;
            staging[destination] = pixels[y * A3_WIDTH + x];
        }
    }
}

bool a3_panel_present(const uint16_t* pixels) {
    if (!active || !retire_transfer(0)) return false;
    if (active->timing.h_size == A3_HEIGHT) {
        if (active->use_graphics_acceleration && rotate_with_ppa(pixels)) {
            if (!enable_dma2d()) return false;
            pixels = rotation.output;
        } else {
            if (!disable_dma2d() || !reserve_staging()) return false;
            rotate_frame(pixels);
            pixels = staging;
        }
    } else if (staging) {
        memcpy(staging, pixels, FrameBytes);
        pixels = staging;
    }
    if (esp_lcd_panel_draw_bitmap(panel, 0, 0, active->timing.h_size, active->timing.v_size,
                                  pixels) != ESP_OK) return false;
    // An asynchronous DMA2D operation owns the submitted buffer until its callback.
    transfer_pending = dma2d_enabled;
    return true;
}

bool a3_panel_wait_first_frame(void) {
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(500) + 1;
    if (!vsync || !retire_transfer(timeout)) return false;
    // Discard an earlier notification. A whole refresh fits between the next two.
    xSemaphoreTake(vsync, 0);
    for (unsigned i = 0; i < 2; ++i) {
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout || xSemaphoreTake(vsync, timeout - elapsed) != pdTRUE) return false;
    }
    return true;
}
