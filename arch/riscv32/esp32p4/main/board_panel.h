#ifndef GTOS_A3_PANEL_H
#define GTOS_A3_PANEL_H

#include "esp_lcd_st7703.h"

// Filled from an approved HC-700BI3002 module specification and its assembly.
typedef struct {
    const st7703_lcd_init_cmd_t* commands;
    uint16_t command_count;
    esp_lcd_video_timing_t timing;
    float pixel_clock_mhz;
    float lane_rate_mbps;
    lcd_color_format_t wire_format;
    uint16_t power_on_ms;
    uint16_t reset_release_ms;
    uint32_t backlight_pwm_hz;
    uint8_t backlight_max_percent; // Zero parks the backlight pin low without PWM.
    bool init_in_command_mode;
    bool portrait_clockwise;
    bool use_graphics_acceleration;
    uint16_t power_off_ms; // Optional hold time after VCI falls.
} A3PanelProfile;

extern const A3PanelProfile gtos_a3_panel_profile;
const A3PanelProfile* a3_panel_profile(void);
bool a3_panel_start(const A3PanelProfile* profile);
bool a3_panel_stop(void);
bool a3_panel_present(const uint16_t* pixels);

#endif
