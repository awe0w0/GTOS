#include "bq25601.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t registers[BQ25601_REG_MAX + 1];

static int16_t read_register(uint8_t address, uint8_t reg, uint8_t* data, uint8_t size) {
    assert(address == BQ25601_I2C_ADDRESS && (size_t)reg + size <= sizeof(registers));
    memcpy(data, registers + reg, size);
    return 1;
}

static int16_t write_register(uint8_t address, uint8_t reg, uint8_t* data, uint8_t size) {
    assert(address == BQ25601_I2C_ADDRESS && (size_t)reg + size <= sizeof(registers));
    memcpy(registers + reg, data, size);
    return 1;
}

static uint16_t read_voltage(uint8_t value) {
    registers[BQ25601_REG04] = value;
    BQ25601_read_register(BQ25601_REG04);
    return BQ25601_get_vreg_mV();
}

int main(void) {
    BQ25601_ctx_t ctx = {
        .BQ25601_i2c_address = BQ25601_I2C_ADDRESS, .read_reg = read_register, .write_reg = write_register,
    };
    BQ25601_init(&ctx);
    assert(read_voltage(0x00) == 3856);
    assert(read_voltage(0x50) == 4176);
    assert(read_voltage(0x58) == 4208);
    assert(read_voltage(0x78) == 4352);
    assert(read_voltage(0xF8) == 4624);
    assert(read_voltage(7) == 3856);

    assert(BQ25601_set_vreg_mV(3855) == 0 && registers[BQ25601_REG04] == 7);
    assert(BQ25601_set_vreg_mV(4200) == 4176 && registers[BQ25601_REG04] == 0x57);
    assert(BQ25601_set_vreg_mV(4351) == 4304 && registers[BQ25601_REG04] == 0x77);
    assert(BQ25601_set_vreg_mV(4352) == 4352 && registers[BQ25601_REG04] == 0x7F);
    assert(BQ25601_set_vreg_mV(4624) == 4624 && registers[BQ25601_REG04] == 0xC7);
    assert(BQ25601_set_vreg_mV(4625) == 0 && registers[BQ25601_REG04] == 0xC7);
    for (uint16_t request = 3856; request <= 4624; ++request) {
        const uint16_t selected = BQ25601_set_vreg_mV(request);
        assert(selected <= request && selected == BQ25601_get_vreg_mV());
        assert((registers[BQ25601_REG04] & 7) == 7);
    }
    puts("BQ25601 REG04 nominal values, rounding, limits and preserved low bits passed");
}
