#include "board_a3.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

enum { PixelCount = A3_WIDTH * A3_HEIGHT, MaxDuty = 1023 };
#ifdef CONFIG_GTOS_A3_SIMULATION
static uint16_t* frame;
#else
#include "board_panel.h"
static const A3PanelProfile* physical;
#endif
static enum { DisplayOff, DisplayOn, DisplayStopping } state;
static bool ready, has_frame;
static unsigned ceiling = 100, power_on_ms = 20, reset_release_ms = 20, power_off_ms;
static int64_t next_power_on_us;
static unsigned brightness = 60;

static bool backlight(unsigned percent) {
    if (!ceiling) return true;
    const unsigned duty = (percent * ceiling * MaxDuty + 5000) / 10000;
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK
        && ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK;
}

void a3_display_init(void) {
    gpio_set_level(A3_PIN_LCD_ON, 0);
    gpio_set_level(A3_PIN_LCD_RESET_ASSERT, 1);
    gpio_set_level(A3_PIN_BACKLIGHT, 0);
    const uint64_t outputs = (1ULL << A3_PIN_LCD_ON) | (1ULL << A3_PIN_LCD_RESET_ASSERT)
        | (1ULL << A3_PIN_BACKLIGHT);
    const gpio_config_t pins = {.pin_bit_mask = outputs, .mode = GPIO_MODE_OUTPUT};
    if (gpio_config(&pins) != ESP_OK) return;
    unsigned pwm_hz = 1000;
#ifndef CONFIG_GTOS_A3_SIMULATION
    physical = a3_panel_profile();
    if (!physical) return;
    pwm_hz = physical->backlight_pwm_hz;
    ceiling = physical->backlight_max_percent;
    power_on_ms = physical->power_on_ms;
    reset_release_ms = physical->reset_release_ms;
    power_off_ms = physical->power_off_ms;
#endif
    if (ceiling) {
        const ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
            .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = pwm_hz, .clk_cfg = LEDC_AUTO_CLK,
        };
        const ledc_channel_config_t channel = {
            .gpio_num = A3_PIN_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0,
        };
        if (ledc_timer_config(&timer) != ESP_OK
            || ledc_channel_config(&channel) != ESP_OK) return;
    }
#ifdef CONFIG_GTOS_A3_SIMULATION
    frame = heap_caps_calloc(PixelCount, sizeof(*frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ready = frame != NULL;
#else
    ready = true;
#endif
}

void gtos_board_display_size(unsigned* width, unsigned* height) {
    if (width) *width = A3_WIDTH;
    if (height) *height = A3_HEIGHT;
}

static bool power_down(void) {
    state = DisplayStopping;
    const bool dark = backlight(0);
    has_frame = false;
#ifndef CONFIG_GTOS_A3_SIMULATION
    if (!a3_panel_stop()) return false;
#endif
    const bool reset = gpio_set_level(A3_PIN_LCD_RESET_ASSERT, 1) == ESP_OK;
    const bool off = gpio_set_level(A3_PIN_LCD_ON, 0) == ESP_OK;
    if (!dark || !reset || !off) return false;
    if (power_off_ms) next_power_on_us = esp_timer_get_time() + (int64_t)power_off_ms * 1000;
    state = DisplayOff;
    return true;
}

static bool power_up(void) {
    if (power_off_ms) {
        const int64_t wait_us = next_power_on_us - esp_timer_get_time();
        if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS((wait_us + 999) / 1000) + 1);
    }
    if (gpio_set_level(A3_PIN_LCD_ON, 1) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(power_on_ms) + 1);
    if (gpio_set_level(A3_PIN_LCD_RESET_ASSERT, 0) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(reset_release_ms) + 1);
#ifndef CONFIG_GTOS_A3_SIMULATION
    return a3_panel_start(physical);
#else
    return true;
#endif
}

int gtos_board_set_display(bool enabled) {
    if (!ready) return GTOS_BOARD_MISSING;
    if (!enabled) return state == DisplayOff || power_down() ? GTOS_BOARD_OK : GTOS_BOARD_IO;
    if (state == DisplayOn) return GTOS_BOARD_OK;
    if (state == DisplayStopping && !power_down()) return GTOS_BOARD_IO;
    if (power_up()) {
        state = DisplayOn;
        return GTOS_BOARD_OK;
    }
    power_down();
    return GTOS_BOARD_IO;
}

int gtos_board_set_brightness(unsigned percent) {
    if (percent > 100) return GTOS_BOARD_INVALID;
    if (!ready) return GTOS_BOARD_MISSING;
    if (state == DisplayOn && has_frame && !backlight(percent)) return GTOS_BOARD_IO;
    brightness = percent;
    return GTOS_BOARD_OK;
}

int gtos_board_present(const uint16_t* pixels, size_t count) {
    if (!pixels || count != PixelCount) return GTOS_BOARD_INVALID;
    if (!ready) return GTOS_BOARD_MISSING;
    if (state != DisplayOn) return GTOS_BOARD_NOT_READY;
#ifdef CONFIG_GTOS_A3_SIMULATION
    memcpy(frame, pixels, count * sizeof(*frame));
#else
    if (!a3_panel_present(pixels)) return GTOS_BOARD_IO;
    if (!has_frame && ceiling && !a3_panel_wait_first_frame()) return GTOS_BOARD_IO;
#endif
    if (!has_frame && !backlight(brightness)) return GTOS_BOARD_IO;
    has_frame = true;
    return GTOS_BOARD_OK;
}

void a3_display_dump(void) {
#ifdef CONFIG_GTOS_A3_SIMULATION
    if (state != DisplayOn || !has_frame) {
        puts("GTOS_FRAME SLEEP");
        return;
    }
    printf("GTOS_FRAME %u %u PWM=%lu\n", A3_WIDTH, A3_HEIGHT,
           (unsigned long)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    for (unsigned y = 0; y < A3_HEIGHT; ++y) {
        const uint16_t* row = frame + y * A3_WIDTH;
        printf("GTOS_ROW");
        for (unsigned x = 0; x < A3_WIDTH;) {
            unsigned end = x + 1;
            while (end < A3_WIDTH && row[x] == row[end]) ++end;
            printf(" %04x/%u", row[x], end - x);
            x = end;
        }
        putchar('\n');
    }
    puts("GTOS_FRAME END");
#else
    puts("GTOS_FRAME UNAVAILABLE");
#endif
}
