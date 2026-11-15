#ifndef GTOS_A3_CHARGE_H
#define GTOS_A3_CHARGE_H

#include <stdint.h>

// Board and pack limits supplied by the hardware build. Values are nominal
// setpoints, selected with the charger's accuracy and the pack limits in mind.
typedef struct {
    uint16_t input_limit_ma;
    uint16_t fast_charge_ma;
    uint16_t precharge_ma;
    uint16_t termination_ma;
    uint16_t regulation_mv;
    uint8_t safety_timer_hours;
} A3ChargeProfile;

extern const A3ChargeProfile gtos_a3_charge_profile;

#endif
