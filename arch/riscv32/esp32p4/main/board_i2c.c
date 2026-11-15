#include "board_a3.h"
#include "bq25601.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "lsm6ds3tr-c_reg.h"
#include "sdkconfig.h"
#include <string.h>

enum { BusTimeoutMs = 50 };
static i2c_master_dev_handle_t imu, bq, cc;
static bool imu_ready, bq_ready, cc_ready, bq_io_failure, charge_gpio_ready, charge_permitted;

static bool read_registers(i2c_master_dev_handle_t device, uint8_t reg,
                           uint8_t* bytes, size_t count) {
    return i2c_master_transmit_receive(device, &reg, 1, bytes, count, BusTimeoutMs) == ESP_OK;
}

static bool write_registers(i2c_master_dev_handle_t device, uint8_t reg,
                            const uint8_t* bytes, size_t count) {
    uint8_t message[17] = {reg};
    if (count > sizeof(message) - 1) return false;
    memcpy(message + 1, bytes, count);
    return i2c_master_transmit(device, message, count + 1, BusTimeoutMs) == ESP_OK;
}

static int32_t imu_read(void* bus, uint8_t reg, uint8_t* bytes, uint16_t count) {
    return read_registers(bus, reg, bytes, count) ? 0 : -1;
}

static int32_t imu_write(void* bus, uint8_t reg, const uint8_t* bytes, uint16_t count) {
    return write_registers(bus, reg, bytes, count) ? 0 : -1;
}

static stmdev_ctx_t imu_context;

static bool init_imu(void) {
    uint8_t who = 0;
    imu_context = (stmdev_ctx_t){.read_reg = imu_read, .write_reg = imu_write, .handle = imu};
    return lsm6ds3tr_c_device_id_get(&imu_context, &who) == 0 && who == LSM6DS3TR_C_ID
        && lsm6ds3tr_c_auto_increment_set(&imu_context, PROPERTY_ENABLE) == 0
        && lsm6ds3tr_c_block_data_update_set(&imu_context, PROPERTY_ENABLE) == 0
        && lsm6ds3tr_c_xl_full_scale_set(&imu_context, LSM6DS3TR_C_2g) == 0
        && lsm6ds3tr_c_gy_full_scale_set(&imu_context, LSM6DS3TR_C_250dps) == 0
        && lsm6ds3tr_c_xl_data_rate_set(&imu_context, LSM6DS3TR_C_XL_ODR_52Hz) == 0
        && lsm6ds3tr_c_gy_data_rate_set(&imu_context, LSM6DS3TR_C_GY_ODR_52Hz) == 0;
}

static int16_t bq_read(uint8_t address, uint8_t reg, uint8_t* bytes, uint8_t count) {
    const bool result = address == BQ25601_I2C_ADDRESS && read_registers(bq, reg, bytes, count);
    bq_io_failure |= !result;
    return result;
}

static int16_t bq_write(uint8_t address, uint8_t reg, uint8_t* bytes, uint8_t count) {
    if (bq_io_failure) return 0;
    const bool result = address == BQ25601_I2C_ADDRESS && write_registers(bq, reg, bytes, count);
    bq_io_failure |= !result;
    return result;
}

static bool init_charger(void) {
    uint8_t id = 0;
    // BQ25601 and BQ25601D have separate part numbers. Select the A3 device.
    if (!read_registers(bq, BQ25601_REG0B, &id, 1) || ((id >> 3) & 15) != 2) return false;
    BQ25601_ctx_t context = {
        .BQ25601_i2c_address = BQ25601_I2C_ADDRESS, .read_reg = bq_read, .write_reg = bq_write,
    };
    bq_io_failure = false;
    BQ25601_init(&context);
    return !bq_io_failure;
}

static bool attach(i2c_master_bus_handle_t bus, int address, i2c_master_dev_handle_t* device) {
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &config, device) == ESP_OK;
}

void a3_i2c_init(void) {
    charge_gpio_ready = gpio_set_level(A3_PIN_CHARGE_EN, 0) == ESP_OK
        && gpio_set_direction(A3_PIN_CHARGE_EN, GPIO_MODE_OUTPUT) == ESP_OK;
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = A3_PIN_SDA, .scl_io_num = A3_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&config, &bus) != ESP_OK) return;
    imu_ready = attach(bus, A3_IMU_ADDRESS, &imu) && init_imu();
    bq_ready = attach(bus, BQ25601_I2C_ADDRESS, &bq) && init_charger();
    cc_ready = attach(bus, A3_USB_CC_ADDRESS, &cc);
}

int gtos_board_read_motion(GtosBoardMotion* motion) {
    if (!motion) return GTOS_BOARD_INVALID;
    if (!imu_ready) return GTOS_BOARD_MISSING;
    lsm6ds3tr_c_status_reg_t status;
    if (lsm6ds3tr_c_status_reg_get(&imu_context, &status) != 0) return GTOS_BOARD_IO;
    if (!status.xlda || !status.gda) return GTOS_BOARD_NOT_READY;
    int16_t acceleration[3], angular_rate[3];
    if (lsm6ds3tr_c_acceleration_raw_get(&imu_context, acceleration) != 0
        || lsm6ds3tr_c_angular_rate_raw_get(&imu_context, angular_rate) != 0)
        return GTOS_BOARD_IO;
    for (int i = 0; i < 3; ++i) {
        motion->acceleration_mg[i] = (int32_t)lsm6ds3tr_c_from_fs2g_to_mg(acceleration[i]);
        motion->angular_rate_mdps[i] = (int32_t)lsm6ds3tr_c_from_fs250dps_to_mdps(angular_rate[i]);
    }
    return GTOS_BOARD_OK;
}

int gtos_board_read_charger(GtosBoardCharger* charger) {
    if (!charger) return GTOS_BOARD_INVALID;
    if (!bq_ready) return GTOS_BOARD_MISSING;
    uint8_t faults;
    bq_io_failure = false;
    BQ25601_read_register(BQ25601_REG00);
    BQ25601_read_register(BQ25601_REG01);
    BQ25601_read_register(BQ25601_REG08);
    if (bq_io_failure || !read_registers(bq, BQ25601_REG09, &faults, 1))
        return GTOS_BOARD_IO;
    *charger = (GtosBoardCharger){
        .input_good = BQ25601_power_good(),
        .charge_enabled = charge_permitted && BQ25601_get_charge_enabled(),
        .charge_state = BQ25601_charge_status(),
        .faults = faults, .input_limit_ma = BQ25601_get_input_current_limit_mA(),
    };
    return GTOS_BOARD_OK;
}

int gtos_board_read_usb(GtosBoardUsb* usb) {
    if (!usb) return GTOS_BOARD_INVALID;
    if (!cc_ready) return GTOS_BOARD_MISSING;
    uint8_t status[2];
    if (!read_registers(cc, 0x08, status, sizeof(status))) return GTOS_BOARD_IO;
    const bool sink = (status[1] >> 6) == 2;
    const uint16_t usb_current_ma[] = {500, 1500, 500, 3000};
    *usb = (GtosBoardUsb){
        .sink_attached = sink, .flipped = sink && (status[1] & 0x20),
        .advertised_ma = sink ? usb_current_ma[(status[0] >> 4) & 3] : 0,
    };
    return GTOS_BOARD_OK;
}

int gtos_board_set_charge(bool enabled) {
    if (!charge_gpio_ready || !bq_ready) return GTOS_BOARD_MISSING;
    if (gpio_set_level(A3_PIN_CHARGE_EN, 0) != ESP_OK) return GTOS_BOARD_IO;
    charge_permitted = false;
#ifndef CONFIG_GTOS_A3_SIMULATION
    if (enabled) return GTOS_BOARD_NOT_READY;
#endif
    if (enabled) {
        GtosBoardUsb usb;
        GtosBoardCharger charger;
        if (gtos_board_read_usb(&usb) || gtos_board_read_charger(&charger)) return GTOS_BOARD_IO;
        if (!usb.sink_attached || !charger.input_good || charger.faults) return GTOS_BOARD_NOT_READY;
        // The virtual board uses 500 mA input, 240 mA charge and 4.167 V CV.
        bq_io_failure = false;
        const unsigned input = BQ25601_set_input_current_limit_mA(500);
        const unsigned current = BQ25601_set_fast_charge_current_mA(240);
        const unsigned voltage = BQ25601_set_vreg_mV(4167);
        if (bq_io_failure || input != 500 || current != 240 || voltage != 4167
            || BQ25601_get_vreg_mV() != voltage)
            return GTOS_BOARD_IO;
    }
    bq_io_failure = false;
    BQ25601_set_charge_enabled(enabled);
    BQ25601_read_register(BQ25601_REG01);
    if (bq_io_failure || BQ25601_get_charge_enabled() != enabled) return GTOS_BOARD_IO;
    if (enabled && gpio_set_level(A3_PIN_CHARGE_EN, 1) != ESP_OK) return GTOS_BOARD_IO;
    charge_permitted = enabled;
    return GTOS_BOARD_OK;
}
