#include "board_a3.h"
#include "board_panel.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "mipi_dsi_priv.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/dma2d_hal.h"
#include "hal/dma2d_ll.h"
#include <assert.h>
#include <stdio.h>

enum { Pixels = A3_WIDTH * A3_HEIGHT };
static enum { NoFailure, InitCommand, DisableDma, DeleteDpi, Draw, SubmitRotation } failure;
static bool phy_open, bus_open, io_open, dpi_open, dpi_active, dma_enabled, ppa_open;
static bool id_read;
static volatile bool copy_pending;
static uint8_t commands[12];
static unsigned command_count, starts, stops, frames;
static esp_lcd_panel_t dpi;
static esp_lcd_panel_io_t dbi;
static esp_lcd_dpi_panel_color_trans_done_cb_t copy_done;
#ifdef GTOS_A3_TEST_BACKLIGHT
enum { ScanoutMarkerPin = 30 };
static esp_lcd_dpi_panel_vsync_cb_t vsync_done;
static volatile unsigned events, event_limit;
#endif
static void* copy_context;
static char phy_token, bus_token;
static const uint16_t* caller_frame;
// No transfer completion or DMA2D-generated pixels are injected into these registers.
static dma2d_dev_t reset_registers;
#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
static bool reject_rx, landscape;
#endif
#ifndef GTOS_A3_TEST_REAL_PPA
static char ppa_token;
static esp_timer_handle_t rotation_timer;
static ppa_event_callback_t rotation_done;
static ppa_srm_oper_config_t rotation_job;
static volatile bool ppa_pending;
static bool hold_rotation;
#endif
static bool test_started;

esp_err_t __real_esp_ldo_acquire_channel(const esp_ldo_channel_config_t* config,
                                        esp_ldo_channel_handle_t* handle);
esp_err_t __real_esp_ldo_release_channel(esp_ldo_channel_handle_t handle);
esp_err_t __real_esp_lcd_new_panel_io_dbi(esp_lcd_dsi_bus_handle_t bus,
                                        const esp_lcd_dbi_io_config_t* config,
                                        esp_lcd_panel_io_handle_t* io);
void __real_dma2d_hal_init(dma2d_hal_context_t* hal, int group_id);

void __wrap_dma2d_hal_init(dma2d_hal_context_t* hal, int group_id) {
    __real_dma2d_hal_init(hal, group_id);
#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
    if (!reject_rx) return;
#endif
    for (unsigned i = 0; i < DMA2D_LL_GET(TX_CHANS_PER_INST); ++i)
        reset_registers.out_channel[i].out_state.out_reset_avail_chn = 1;
    for (unsigned i = 0; i < DMA2D_LL_GET(RX_CHANS_PER_INST); ++i)
        reset_registers.in_channel[i].in_state.in_reset_avail_chn = 1;
#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
    reset_registers.in_channel[0].in_state.in_reset_avail_chn = 0;
#endif
    hal->dev = &reset_registers;
}

esp_err_t __wrap_esp_ldo_acquire_channel(const esp_ldo_channel_config_t* config,
                                        esp_ldo_channel_handle_t* handle) {
    if (!test_started) return __real_esp_ldo_acquire_channel(config, handle);
    assert(!phy_open && config->chan_id == 3 && config->voltage_mv == 2500);
    phy_open = true;
    *handle = (esp_ldo_channel_handle_t)&phy_token;
    return ESP_OK;
}

esp_err_t __wrap_esp_ldo_release_channel(esp_ldo_channel_handle_t handle) {
    if (handle != (esp_ldo_channel_handle_t)&phy_token) return __real_esp_ldo_release_channel(handle);
    assert(phy_open && !bus_open);
    phy_open = false;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t* config,
                                   esp_lcd_dsi_bus_handle_t* handle) {
    assert(phy_open && !bus_open && config->bus_id == 0 && config->num_data_lanes == 2);
    assert(config->lane_bit_rate_mbps == 500);
    bus_open = true;
    *handle = (esp_lcd_dsi_bus_handle_t)&bus_token;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_del_dsi_bus(esp_lcd_dsi_bus_handle_t handle) {
    assert(bus_open && !io_open && !dpi_open && handle == (esp_lcd_dsi_bus_handle_t)&bus_token);
    bus_open = false;
    return ESP_OK;
}

static esp_err_t send_command(esp_lcd_panel_io_t* handle, int command, const void* data, size_t size) {
    assert(handle == &dbi && io_open && dpi_open && id_read && command_count < sizeof(commands));
    commands[command_count++] = command;
    if (command == LCD_CMD_COLMOD) assert(size == 1 && *(const uint8_t*)data == 0x77);
    if (command == LCD_CMD_SLPOUT && failure == InitCommand) {
        failure = NoFailure;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t read_id(esp_lcd_panel_io_t* handle, int command, void* data, size_t size) {
    assert(handle == &dbi && io_open && dpi_open && !id_read && !command_count);
    assert(command == 0x04 && data && size == 3);
    uint8_t* id = data;
    id[0] = 0x12;
    id[1] = 0x34;
    id[2] = 0x56;
    id_read = true;
    return ESP_OK;
}

static esp_err_t delete_dbi(esp_lcd_panel_io_t* handle) {
    assert(handle == &dbi && io_open && !dpi_open);
    io_open = false;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_new_panel_io_dbi(esp_lcd_dsi_bus_handle_t handle,
                                        const esp_lcd_dbi_io_config_t* config,
                                        esp_lcd_panel_io_handle_t* io) {
    assert(bus_open && !io_open && handle == (esp_lcd_dsi_bus_handle_t)&bus_token);
    assert(config->lcd_cmd_bits == 8 && config->lcd_param_bits == 8);
    command_count = 0;
    id_read = false;
    dbi = (esp_lcd_panel_io_t){.rx_param = read_id, .tx_param = send_command, .del = delete_dbi};
    io_open = true;
    *io = &dbi;
    return ESP_OK;
}

static esp_err_t start_dpi(esp_lcd_panel_t* handle) {
    assert(handle == &dpi && dpi_open && !dpi_active);
    assert(command_count == 3 && commands[0] == LCD_CMD_MADCTL
           && commands[1] == LCD_CMD_COLMOD && commands[2] == LCD_CMD_SLPOUT);
    dpi_active = true;
    ++starts;
    return ESP_OK;
}

static esp_err_t delete_dpi(esp_lcd_panel_t* handle) {
    assert(handle == &dpi && dpi_open && !dma_enabled && !copy_pending);
    if (failure == DeleteDpi) {
        failure = NoFailure;
        return ESP_ERR_INVALID_STATE;
    }
    if (dpi_active) assert(commands[command_count - 1] == LCD_CMD_DISPOFF);
    dpi_open = dpi_active = false;
    copy_done = NULL;
    copy_context = NULL;
#ifdef GTOS_A3_TEST_BACKLIGHT
    vsync_done = NULL;
#endif
    ++stops;
    return ESP_OK;
}

static esp_err_t draw_dpi(esp_lcd_panel_t* handle, int x0, int y0, int x1, int y1, const void* pixels) {
    assert(handle == &dpi && dpi_active && !copy_pending);
    assert(commands[command_count - 1] == LCD_CMD_DISPON);
#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
    if (landscape) {
        assert(x0 == 0 && y0 == 0 && x1 == A3_WIDTH && y1 == A3_HEIGHT);
        assert(pixels == caller_frame && !dma_enabled && !copy_done);
        ++frames;
        return ESP_OK;
    }
#endif
    assert(x0 == 0 && y0 == 0 && x1 == A3_HEIGHT && y1 == A3_WIDTH);
    if (failure == Draw) {
        failure = NoFailure;
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t* rotated = pixels;
    assert(rotated[0] == caller_frame[Pixels - A3_WIDTH] && rotated[A3_HEIGHT - 1] == caller_frame[0]);
    assert(rotated[Pixels - A3_HEIGHT] == caller_frame[Pixels - 1] && rotated[Pixels - 1] == caller_frame[A3_WIDTH - 1]);
#ifndef GTOS_A3_TEST_REAL_PPA
    if (ppa_pending) assert(!dma_enabled && pixels != rotation_job.out.buffer);
#endif
    if (dma_enabled) copy_pending = true;
    else if (copy_done) assert(!copy_done(&dpi, NULL, copy_context));
    ++frames;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_new_panel_dpi(esp_lcd_dsi_bus_handle_t handle,
                                     const esp_lcd_dpi_panel_config_t* config,
                                     esp_lcd_panel_handle_t* panel) {
    assert(bus_open && io_open && !dpi_open && handle == (esp_lcd_dsi_bus_handle_t)&bus_token);
#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
    if (landscape)
        assert(config->video_timing.h_size == A3_WIDTH && config->video_timing.v_size == A3_HEIGHT);
    else
#endif
        assert(config->video_timing.h_size == A3_HEIGHT && config->video_timing.v_size == A3_WIDTH);
    assert(config->in_color_format == LCD_COLOR_FMT_RGB565 && config->out_color_format == LCD_COLOR_FMT_RGB888);
    dpi = (esp_lcd_panel_t){.init = start_dpi, .del = delete_dpi, .draw_bitmap = draw_dpi};
    dpi_open = true;
    *panel = &dpi;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t handle,
        const esp_lcd_dpi_panel_event_callbacks_t* callbacks, void* context) {
    assert(handle == &dpi && dpi_active);
#ifdef GTOS_A3_TEST_BACKLIGHT
    assert(callbacks->on_vsync);
    vsync_done = callbacks->on_vsync;
#else
    assert(callbacks->on_color_trans_done && context && !callbacks->on_vsync);
#endif
    copy_done = callbacks->on_color_trans_done;
    copy_context = context;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_dpi_panel_enable_dma2d(esp_lcd_panel_handle_t handle) {
    assert(handle == &dpi && dpi_active && !dma_enabled && copy_done);
    dma_enabled = true;
    return ESP_OK;
}

esp_err_t __wrap_esp_lcd_dpi_panel_disable_dma2d(esp_lcd_panel_handle_t handle) {
    assert(handle == &dpi && dpi_active && dma_enabled && !copy_pending);
    if (failure == DisableDma) {
        failure = NoFailure;
        return ESP_ERR_INVALID_STATE;
    }
    dma_enabled = false;
    return ESP_OK;
}

#ifdef GTOS_A3_TEST_REAL_PPA
esp_err_t __real_ppa_register_client(const ppa_client_config_t* config, ppa_client_handle_t* handle);
esp_err_t __real_ppa_unregister_client(ppa_client_handle_t handle);
esp_err_t __real_ppa_client_register_event_callbacks(ppa_client_handle_t handle, const ppa_event_callbacks_t* callbacks);
esp_err_t __real_ppa_do_scale_rotate_mirror(ppa_client_handle_t handle, const ppa_srm_oper_config_t* config);
#else
static void complete_rotation(void* unused) {
    (void)unused;
    assert(ppa_pending && rotation_done);
    const uint16_t* source = rotation_job.in.buffer;
    uint16_t* result = rotation_job.out.buffer;
    result[0] = source[Pixels - A3_WIDTH];
    result[A3_HEIGHT - 1] = source[0];
    result[Pixels - A3_HEIGHT] = source[Pixels - 1];
    result[Pixels - 1] = source[A3_WIDTH - 1];
    ppa_pending = false;
    if (rotation_done((ppa_client_handle_t)&ppa_token, NULL, rotation_job.user_data))
        esp_timer_isr_dispatch_need_yield();
}
#endif

esp_err_t __wrap_ppa_register_client(const ppa_client_config_t* config, ppa_client_handle_t* handle) {
#ifdef GTOS_A3_TEST_REAL_PPA
    return __real_ppa_register_client(config, handle);
#else
    assert(!ppa_open && dpi_active && config->oper_type == PPA_OPERATION_SRM && config->max_pending_trans_num == 1);
    const esp_timer_create_args_t timer = {.callback = complete_rotation, .dispatch_method = ESP_TIMER_ISR};
    assert(esp_timer_create(&timer, &rotation_timer) == ESP_OK);
    ppa_open = true;
    *handle = (ppa_client_handle_t)&ppa_token;
    return ESP_OK;
#endif
}

esp_err_t __wrap_ppa_unregister_client(ppa_client_handle_t handle) {
#ifdef GTOS_A3_TEST_REAL_PPA
    return __real_ppa_unregister_client(handle);
#else
    assert(ppa_open && !ppa_pending && !dpi_open && handle == (ppa_client_handle_t)&ppa_token);
    assert(esp_timer_delete(rotation_timer) == ESP_OK);
    rotation_timer = NULL;
    rotation_done = NULL;
    ppa_open = false;
    return ESP_OK;
#endif
}

esp_err_t __wrap_ppa_client_register_event_callbacks(ppa_client_handle_t handle, const ppa_event_callbacks_t* callbacks) {
#ifdef GTOS_A3_TEST_REAL_PPA
    return __real_ppa_client_register_event_callbacks(handle, callbacks);
#else
    assert(ppa_open && handle == (ppa_client_handle_t)&ppa_token && callbacks->on_trans_done);
    rotation_done = callbacks->on_trans_done;
    return ESP_OK;
#endif
}

esp_err_t __wrap_ppa_do_scale_rotate_mirror(ppa_client_handle_t handle, const ppa_srm_oper_config_t* config) {
#ifdef GTOS_A3_TEST_REAL_PPA
    puts("GTOS_A3_PANEL_TEST SDK_PPA_SUBMIT");
    const esp_err_t result = __real_ppa_do_scale_rotate_mirror(handle, config);
    printf("GTOS_A3_PANEL_TEST SDK_PPA_RETURN result=%ld\n", (long)result);
    return result;
#else
    assert(ppa_open && handle == (ppa_client_handle_t)&ppa_token && !copy_pending && !ppa_pending);
    assert(config->in.buffer != caller_frame && config->out.buffer != config->in.buffer);
    assert(config->in.pic_w == A3_WIDTH && config->in.pic_h == A3_HEIGHT);
    assert(config->out.pic_w == A3_HEIGHT && config->out.pic_h == A3_WIDTH);
    assert(config->mode == PPA_TRANS_MODE_NON_BLOCKING && config->rotation_angle == PPA_SRM_ROTATION_ANGLE_270);
    if (failure == SubmitRotation) {
        failure = NoFailure;
        return ESP_FAIL;
    }
    rotation_job = *config;
    ppa_pending = true;
    if (!hold_rotation) assert(esp_timer_start_once(rotation_timer, 1000) == ESP_OK);
    return ESP_OK;
#endif
}

#if !defined(GTOS_A3_TEST_REAL_PPA) && !defined(GTOS_A3_TEST_DMA2D_UNAVAILABLE)
static void complete_copy(void* unused) {
    (void)unused;
    assert(copy_pending && copy_done);
    copy_pending = false;
    if (copy_done(&dpi, NULL, copy_context)) esp_timer_isr_dispatch_need_yield();
}
#endif

static void assert_closed(void) {
    assert(!phy_open && !bus_open && !io_open && !dpi_open && !dma_enabled && !ppa_open);
    assert(!copy_pending);
}

#ifdef GTOS_A3_TEST_DMA2D_UNAVAILABLE
static void check_dma2d_unavailable(uint16_t* pixels) {
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && frames == 1);
    assert(!ppa_open && !dma_enabled && !copy_done && !dma2d_ll_is_bus_clock_enabled(0));
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    puts("GTOS_A3_PANEL_TEST DMA2D_MMIO_FALLBACK PASS");

    reject_rx = true;
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && frames == 2);
    assert(!ppa_open && !dma_enabled && !copy_done && !dma2d_ll_is_bus_clock_enabled(0));
    assert(!reset_registers.rst_conf.clk_en);
    for (unsigned i = 0; i < DMA2D_LL_GET(TX_CHANS_PER_INST); ++i)
        assert(!reset_registers.out_channel[i].out_conf0.out_cmd_disable_chn);
    for (unsigned i = 0; i < DMA2D_LL_GET(RX_CHANS_PER_INST); ++i)
        assert(!reset_registers.in_channel[i].in_conf0.in_cmd_disable_chn);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    puts("GTOS_A3_PANEL_TEST DMA2D_RX_FALLBACK PASS");

    A3PanelProfile profile = gtos_a3_panel_profile;
    profile.timing.h_size = A3_WIDTH;
    profile.timing.v_size = A3_HEIGHT;
    landscape = true;
    reject_rx = false;
    assert(a3_panel_start(&profile));
    assert(a3_panel_present(pixels) && frames == 3);
    assert(a3_panel_stop());
    assert_closed();
    puts("GTOS_A3_PANEL_TEST DMA2D_LANDSCAPE_FALLBACK PASS");
}
#endif

#ifdef GTOS_A3_TEST_PPA_RECOVERY
static void finish_copy(void) {
    const esp_timer_create_args_t config = {.callback = complete_copy, .dispatch_method = ESP_TIMER_ISR};
    esp_timer_handle_t timer;
    assert(esp_timer_create(&config, &timer) == ESP_OK);
    assert(esp_timer_start_once(timer, 1000) == ESP_OK);
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    assert(!copy_pending && esp_timer_delete(timer) == ESP_OK);
}

static void check_ppa_recovery(uint16_t* pixels) {
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && dma_enabled && copy_pending && frames == 1);
    finish_copy();

    hold_rotation = true;
    const TickType_t started = xTaskGetTickCount();
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK);
    const TickType_t elapsed = xTaskGetTickCount() - started;
    assert(elapsed >= pdMS_TO_TICKS(100) && elapsed < pdMS_TO_TICKS(250));
    assert(ppa_pending && !dma_enabled && !copy_pending && frames == 2);
    const uint16_t* pending_input = rotation_job.in.buffer;
    const void* pending_output = rotation_job.out.buffer;
    pixels[0] = 0xffe0;
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && frames == 3);
    assert(pending_input[0] == 0xf800);
    puts("GTOS_A3_PANEL_TEST PPA_TIMEOUT_CPU_COPY PASS");

    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert(!phy_open && !bus_open && !io_open && !dpi_open && ppa_open && ppa_pending);
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && frames == 4 && !dma_enabled);
    assert(rotation_job.out.buffer == pending_output && pending_input[0] == 0xf800);
    assert(esp_timer_start_once(rotation_timer, 1000) == ESP_OK);
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    assert(!ppa_pending && frames == 4);
    hold_rotation = false;
    pixels[0] = 0x1234;
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && dma_enabled && copy_pending && frames == 5);
    finish_copy();

    failure = SubmitRotation;
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && frames == 6 && failure == NoFailure);
    assert(!ppa_pending && !dma_enabled && !copy_pending);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    puts("GTOS_A3_PANEL_TEST PPA_LATE_COMPLETION_RECOVERY PASS");
}
#endif

#ifdef GTOS_A3_TEST_BACKLIGHT
static void emit_vsync(void* unused) {
    (void)unused;
    assert(vsync_done);
    if (vsync_done(&dpi, NULL, copy_context)) esp_timer_isr_dispatch_need_yield();
}

static void first_frame_events(void* unused) {
    (void)unused;
    if (events >= event_limit) return;
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    switch (events++) {
        case 0:
            assert(copy_pending);
            emit_vsync(NULL);
            break;
        case 1:
            complete_copy(NULL);
            break;
        case 2:
            emit_vsync(NULL);
            break;
        case 3:
            assert(gpio_set_level(ScanoutMarkerPin, 1) == ESP_OK);
            emit_vsync(NULL);
            break;
    }
}

static esp_timer_handle_t create_isr_timer(esp_timer_cb_t callback) {
    const esp_timer_create_args_t config = {.callback = callback, .dispatch_method = ESP_TIMER_ISR};
    esp_timer_handle_t timer;
    assert(esp_timer_create(&config, &timer) == ESP_OK);
    return timer;
}

static void expect_frame_timeout(const uint16_t* pixels) {
    const TickType_t started = xTaskGetTickCount();
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_IO);
    const TickType_t elapsed = xTaskGetTickCount() - started;
    assert(elapsed >= pdMS_TO_TICKS(500) && elapsed <= pdMS_TO_TICKS(500) + 2);
}

static void check_backlight(const uint16_t* pixels) {
    assert(gpio_set_level(ScanoutMarkerPin, 0) == ESP_OK);
    assert(gpio_set_direction(ScanoutMarkerPin, GPIO_MODE_OUTPUT) == ESP_OK);
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    printf("GTOS_A3_PANEL_TEST BEFORE_FRAME duty=%lu\n",
           (unsigned long)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    assert(gtos_board_set_brightness(75) == GTOS_BOARD_OK);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);

    failure = Draw;
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_IO && !copy_pending && failure == NoFailure);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    expect_frame_timeout(pixels);
    assert(copy_pending);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    esp_timer_handle_t timer = create_isr_timer(complete_copy);
    assert(esp_timer_start_once(timer, 10000) == ESP_OK);
    vTaskDelay(pdMS_TO_TICKS(30) + 1);
    assert(!copy_pending && esp_timer_delete(timer) == ESP_OK);
    puts("GTOS_A3_PANEL_TEST DRAW_AND_COPY_FAILURE_DARK PASS");

    timer = create_isr_timer(first_frame_events);
    events = 0;
    event_limit = 3;
    assert(esp_timer_start_periodic(timer, 20000) == ESP_OK);
    expect_frame_timeout(pixels);
    assert(!copy_pending);
    assert(esp_timer_stop(timer) == ESP_OK && events == 3);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    puts("GTOS_A3_PANEL_TEST INCOMPLETE_SCANOUT_DARK PASS");

    events = 0;
    event_limit = 4;
    assert(esp_timer_start_periodic(timer, 20000) == ESP_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK && !copy_pending);
    assert(esp_timer_stop(timer) == ESP_OK && events == 4);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 767);
    assert(esp_timer_delete(timer) == ESP_OK);
    vTaskDelay(pdMS_TO_TICKS(40) + 1);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == 0);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();

    A3PanelProfile software = gtos_a3_panel_profile;
    software.use_graphics_acceleration = false;
    assert(a3_panel_start(&software));
    assert(a3_panel_present(pixels));
    timer = create_isr_timer(emit_vsync);
    assert(esp_timer_start_periodic(timer, 20000) == ESP_OK);
    assert(a3_panel_wait_first_frame());
    assert(esp_timer_stop(timer) == ESP_OK && esp_timer_delete(timer) == ESP_OK);
    assert(a3_panel_stop());
    assert_closed();
    puts("GTOS_A3_PANEL_TEST FIRST_FRAME_BACKLIGHT PASS");
}
#endif

static void check_dbi_packets(void) {
    static dsi_host_dev_t host;
    struct esp_lcd_dsi_bus_t test_bus = {.hal.host = &host};
    const esp_lcd_dbi_io_config_t config = {.virtual_channel = 1, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    esp_lcd_panel_io_handle_t handle;
    assert(__real_esp_lcd_new_panel_io_dbi(&test_bus, &config, &handle) == ESP_OK);

    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    assert(esp_lcd_panel_io_tx_param(handle, LCD_CMD_DISPON, NULL, 0) == ESP_OK);
    assert(host.gen_hdr.val == 0x002945);
    assert(esp_lcd_panel_io_tx_param(handle, LCD_CMD_MADCTL, data, 1) == ESP_OK);
    assert(host.gen_hdr.val == 0x113655);
    assert(esp_lcd_panel_io_tx_param(handle, 0xb9, data, 2) == ESP_OK);
    assert(host.gen_hdr.val == 0x000379 && host.gen_pld_data.val == 0x002211b9);
    assert(esp_lcd_panel_io_tx_param(handle, 0xb9, data, sizeof(data)) == ESP_OK);
    assert(host.gen_hdr.val == 0x000679 && host.gen_pld_data.val == 0x00005544);

    host.cmd_pkt_status.gen_pld_w_full = 1;
    int64_t started = esp_timer_get_time();
    assert(esp_lcd_panel_io_tx_param(handle, 0xb9, data, 2) == ESP_ERR_TIMEOUT);
    int64_t elapsed = esp_timer_get_time() - started;
    assert(elapsed >= 100000 && elapsed < 150000);
    assert(host.gen_hdr.val == 0x000679 && host.gen_pld_data.val == 0x00005544);
    assert(esp_lcd_panel_io_tx_param(handle, 0xb9, data, UINT16_MAX) == ESP_ERR_INVALID_ARG);
    uint8_t response[3] = {0, 0, 0};
    assert(esp_lcd_panel_io_rx_param(handle, 0x04, response, sizeof(response)) == ESP_ERR_INVALID_RESPONSE);
    assert(host.gen_hdr.val == 0x000679 && response[0] == 0 && response[1] == 0 && response[2] == 0);
    assert(esp_lcd_panel_io_del(handle) == ESP_OK);
    puts("GTOS_A3_PANEL_TEST DBI_PACKETS PASS");
}

void app_main(void) {
    check_dbi_packets();
    uint16_t* pixels = heap_caps_calloc(Pixels, sizeof(*pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(pixels);
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    pixels[0] = 0xf800;
    pixels[A3_WIDTH - 1] = 0x07e0;
    pixels[Pixels - A3_WIDTH] = 0x001f;
    pixels[Pixels - 1] = 0xffff;
    caller_frame = pixels;
    test_started = true;
    a3_display_init();
    assert(gtos_board_set_brightness(100) == GTOS_BOARD_OK);

#if defined(GTOS_A3_TEST_REAL_PPA)
    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    const int result = gtos_board_present(pixels, Pixels);
    printf("GTOS_A3_PANEL_TEST PRESENT_RETURN result=%d\n", result);
    assert(result == GTOS_BOARD_OK && !dma_enabled && !copy_pending && frames == 1);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    heap_caps_free(pixels);
    puts("GTOS_A3_PANEL_TEST SDK_PPA_TIMEOUT_FALLBACK PASS");
    return;
#elif defined(GTOS_A3_TEST_DMA2D_UNAVAILABLE)
    check_dma2d_unavailable(pixels);
#elif defined(GTOS_A3_TEST_PPA_RECOVERY)
    check_ppa_recovery(pixels);
#elif defined(GTOS_A3_TEST_BACKLIGHT)
    check_backlight(pixels);
#else
    failure = InitCommand;
    assert(gtos_board_set_display(true) == GTOS_BOARD_IO);
    assert(failure == NoFailure);
    assert_closed();
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_NOT_READY);
    puts("GTOS_A3_PANEL_TEST INIT_ROLLBACK PASS");

    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_OK);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_IO && frames == 1);
    assert(gtos_board_set_display(false) == GTOS_BOARD_IO && copy_pending && dma_enabled && dpi_open);
    assert(gtos_board_present(pixels, Pixels) == GTOS_BOARD_NOT_READY);
    assert(gtos_board_set_brightness(75) == GTOS_BOARD_OK);

    const esp_timer_create_args_t timer_config = {.callback = complete_copy, .dispatch_method = ESP_TIMER_ISR};
    esp_timer_handle_t timer;
    assert(esp_timer_create(&timer_config, &timer) == ESP_OK);
    assert(esp_timer_start_once(timer, 10000) == ESP_OK);
    failure = DisableDma;
    assert(gtos_board_set_display(false) == GTOS_BOARD_IO && dma_enabled && dpi_open && io_open);
    failure = DeleteDpi;
    assert(gtos_board_set_display(false) == GTOS_BOARD_IO && !dma_enabled && dpi_open && io_open);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    assert(esp_timer_delete(timer) == ESP_OK);
    puts("GTOS_A3_PANEL_TEST DMA_AND_DELETE_RETRY PASS");

    assert(gtos_board_set_display(true) == GTOS_BOARD_OK);
    assert(gtos_board_set_display(false) == GTOS_BOARD_OK);
    assert_closed();
    assert(starts == 2 && stops == 3);

    A3PanelProfile software = gtos_a3_panel_profile;
    software.use_graphics_acceleration = false;
    assert(a3_panel_start(&software));
    assert(a3_panel_present(pixels) && frames == 2);
    assert(a3_panel_stop());
    assert_closed();
#endif
    assert(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == free_psram);
    heap_caps_free(pixels);
    puts("GTOS_A3_PANEL_TEST PASS");
}
