#include "board_panel.h"

// Synthetic transport-test values for ESP-EMU.
static const st7703_lcd_init_cmd_t commands[] = {
    {.cmd = 0x11, .delay_ms = 120},
};

const A3PanelProfile gtos_a3_panel_profile = {
    .commands = commands,
    .command_count = sizeof(commands) / sizeof(commands[0]),
    .timing = {
        .h_size = 280, .v_size = 1424, .hsync_back_porch = 16, .hsync_front_porch = 16,
        .hsync_pulse_width = 8, .vsync_back_porch = 6, .vsync_front_porch = 6, .vsync_pulse_width = 4,
    },
    .pixel_clock_mhz = 30,
    .lane_rate_mbps = 500,
    .wire_format = LCD_COLOR_FMT_RGB888,
    .power_on_ms = 20,
    .reset_release_ms = 20,
    .power_off_ms = 50,
    .init_in_command_mode = true,
    .portrait_clockwise = true,
    .use_graphics_acceleration = true,
};
