#include "board_a3.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"

static pcnt_unit_handle_t counter;
static bool buttons_ready;
static uint32_t stable, pending;
static int64_t pending_since;

static bool init_encoder(void) {
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t a, b;
    const pcnt_unit_config_t config = {
        .low_limit = -30000, .high_limit = 30000, .flags.accum_count = true,
    };
    const pcnt_chan_config_t a_config = {
        .edge_gpio_num = A3_PIN_ENC_A, .level_gpio_num = A3_PIN_ENC_B,
    };
    const pcnt_chan_config_t b_config = {
        .edge_gpio_num = A3_PIN_ENC_B, .level_gpio_num = A3_PIN_ENC_A,
    };
    if (pcnt_new_unit(&config, &unit) != ESP_OK) return false;
    if (pcnt_new_channel(unit, &a_config, &a) != ESP_OK) {
        pcnt_del_unit(unit);
        return false;
    }
    if (pcnt_new_channel(unit, &b_config, &b) != ESP_OK) {
        pcnt_del_channel(a);
        pcnt_del_unit(unit);
        return false;
    }
    if (pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                    PCNT_CHANNEL_EDGE_ACTION_INCREASE) != ESP_OK
        || pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                        PCNT_CHANNEL_EDGE_ACTION_DECREASE) != ESP_OK
        || pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK
        || pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK
        || pcnt_unit_add_watch_point(unit, config.low_limit) != ESP_OK
        || pcnt_unit_add_watch_point(unit, config.high_limit) != ESP_OK
        || pcnt_unit_enable(unit) != ESP_OK
        || pcnt_unit_clear_count(unit) != ESP_OK || pcnt_unit_start(unit) != ESP_OK) {
        pcnt_unit_disable(unit);
        pcnt_del_channel(b);
        pcnt_del_channel(a);
        pcnt_del_unit(unit);
        return false;
    }
    counter = unit;
    return true;
}

void a3_input_init(void) {
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << A3_PIN_WAKE) | (1ULL << A3_PIN_NEXT) | (1ULL << A3_PIN_BACK)
            | (1ULL << A3_PIN_ENC_A) | (1ULL << A3_PIN_ENC_B) | (1ULL << A3_PIN_ENC_SW),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    buttons_ready = gpio_config(&config) == ESP_OK;
    if (buttons_ready) init_encoder();
}

int gtos_board_read_input(GtosBoardInput* input) {
    if (!input) return GTOS_BOARD_INVALID;
    if (!buttons_ready) return GTOS_BOARD_MISSING;
    uint32_t raw = 0, pressed = 0;
    if (!gpio_get_level(A3_PIN_WAKE)) raw |= GTOS_BOARD_WAKE;
    if (!gpio_get_level(A3_PIN_NEXT)) raw |= GTOS_BOARD_NEXT;
    if (!gpio_get_level(A3_PIN_BACK)) raw |= GTOS_BOARD_BACK;
    if (!gpio_get_level(A3_PIN_ENC_SW)) raw |= GTOS_BOARD_KNOB;
    const int64_t now = esp_timer_get_time();
    if (raw != pending) {
        pending = raw;
        pending_since = now;
    } else if (stable != pending && now - pending_since >= 20000) {
        pressed = pending & ~stable;
        stable = pending;
    }
    int pulses = 0;
    if (counter && pcnt_unit_get_count(counter, &pulses) != ESP_OK) return GTOS_BOARD_IO;
    *input = (GtosBoardInput){.buttons = stable, .pressed = pressed, .encoder_steps = pulses / 4};
    return GTOS_BOARD_OK;
}
