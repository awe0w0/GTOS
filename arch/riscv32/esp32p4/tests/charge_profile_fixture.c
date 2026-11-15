#include "board_charge.h"

// Synthetic register-test values for ESP-EMU. There is no qualified pack here.
const A3ChargeProfile gtos_a3_charge_profile = {
    .input_limit_ma = 1800,
    .fast_charge_ma = 360,
    .precharge_ma = 120,
    .termination_ma = 60,
    .regulation_mv = 4200,
    .safety_timer_hours = 5,
};
