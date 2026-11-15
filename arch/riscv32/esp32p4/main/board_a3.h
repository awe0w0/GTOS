#ifndef GTOS_A3_BOARD_INTERNAL_H
#define GTOS_A3_BOARD_INTERNAL_H

#include <drivers/board.h>

// A3 wiring. Numbers are GPIOs, with active levels measured at the P4 pin.
enum {
    A3_PIN_SDA = 7,
    A3_PIN_SCL = 8,
    A3_PIN_WAKE = 2,
    A3_PIN_NEXT = 11,
    A3_PIN_BACK = 12,
    A3_PIN_IMU_INT = 3,
    A3_PIN_CC_INT_N = 4,
    A3_PIN_CHARGER_PG_N = 9,
    A3_PIN_CHARGER_INT_N = 29,
    A3_PIN_ENC_A = 20,
    A3_PIN_ENC_B = 21,
    A3_PIN_ENC_SW = 22,
    A3_PIN_CHARGE_EN = 28,
    A3_PIN_LCD_ON = 10,
    A3_PIN_LCD_RESET_ASSERT = 27,
    A3_PIN_BACKLIGHT = 26,
    A3_PIN_RF_ON = 5,
    A3_PIN_C6_RESET_ASSERT = 6,
    A3_IMU_ADDRESS = 0x6a,
    A3_USB_CC_ADDRESS = 0x47,
    A3_WIDTH = 1424,
    A3_HEIGHT = 280,
};

void a3_i2c_init(void);
void a3_input_init(void);
bool a3_wake_held_at_boot(void);
bool a3_c6_power_hold_reset(void);
bool a3_c6_reset(void);
bool a3_c6_power_off(void);
void a3_storage_init(void);
void a3_display_init(void);
void a3_display_dump(void);

#endif
