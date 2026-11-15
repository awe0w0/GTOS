#include "board_a3.h"
#include "board_charge.h"
#include "bq25601.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "lsm6ds3tr-c_reg.h"
#include "sdkconfig.h"
#include <string.h>

enum { BusTimeoutMs = 50, WatchdogFault = 0x80 };
static i2c_master_dev_handle_t imu, bq, cc;
static bool imu_ready, bq_ready, cc_ready, bq_io_failure, charge_gpio_ready, charge_permitted;
static A3ChargeProfile active_charge;
#ifndef CONFIG_GTOS_A3_SIMULATION
static bool charger_pg_ready;
#endif

#if defined(CONFIG_GTOS_A3_SIMULATION) && defined(GTOS_A3_CHARGE_PROFILE)
#error GTOS_A3_CHARGE_PROFILE requires sdkconfig.defaults.hardware
#endif

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
    BQ25601_set_charge_enabled(false);
    return !bq_io_failure && !BQ25601_get_charge_enabled();
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
#ifndef CONFIG_GTOS_A3_SIMULATION
    const gpio_config_t pg = {
        .pin_bit_mask = 1ULL << A3_PIN_CHARGER_PG_N, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    charger_pg_ready = gpio_config(&pg) == ESP_OK;
#endif
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

static bool power_good_pin(void) {
#ifdef CONFIG_GTOS_A3_SIMULATION
    return true;
#else
    return charger_pg_ready && gpio_get_level(A3_PIN_CHARGER_PG_N) == 0;
#endif
}

static charge_time_enum safety_timer(const A3ChargeProfile* profile) {
    return profile->safety_timer_hours == 5 ? CHG_TIMER_5_HRS : CHG_TIMER_10_HRS;
}

static bool read_charger_registers(bool settings, uint8_t* faults) {
    bq_io_failure = false;
    const uint8_t last = settings ? BQ25601_REG05 : BQ25601_REG01;
    for (uint8_t reg = BQ25601_REG00; reg <= last && !bq_io_failure; ++reg)
        BQ25601_read_register(reg);
    if (settings) BQ25601_read_register(BQ25601_REG07);
    BQ25601_read_register(BQ25601_REG08);
    return !bq_io_failure && read_registers(bq, BQ25601_REG09, faults, 1);
}

static bool charge_settings_match(const A3ChargeProfile* profile) {
    return !BQ25601_get_otg_enabled() && BQ25601_get_input_current_limit_mA() == profile->input_limit_ma
        && BQ25601_get_fast_charge_current_mA() == profile->fast_charge_ma
        && BQ25601_get_pre_charge_current_mA() == profile->precharge_ma
        && BQ25601_get_charge_termination_current_mA() == profile->termination_ma
        && BQ25601_get_vreg_mV() == profile->regulation_mv
        && BQ25601_get_watchdog_time() == WATCHDOG_DISABLED
        && BQ25601_get_topoff_timer() == TOPOFF_TIMER_DISABLED
        && BQ25601_get_enable_termination() && BQ25601_get_enable_timer()
        && BQ25601_get_charge_timer() == safety_timer(profile)
        && !BQ25601_get_x2_slow_safety_timer();
}

int gtos_board_read_charger(GtosBoardCharger* charger) {
    if (!charger) return GTOS_BOARD_INVALID;
    if (!bq_ready) return GTOS_BOARD_MISSING;
    uint8_t faults = 0;
    const bool read_ok = read_charger_registers(charge_permitted, &faults);
    const bool input_good = power_good_pin() && BQ25601_power_good();
    const bool charge_configured = BQ25601_get_charge_enabled();
    // Checking here also catches a latched fault consumed by an application's read.
    if (charge_permitted && (!read_ok || !input_good || faults || !charge_configured
                             || !charge_settings_match(&active_charge))) {
        const int result = gtos_board_set_charge(false);
        if (result != GTOS_BOARD_OK) return result;
    }
    if (!read_ok) return GTOS_BOARD_IO;
    *charger = (GtosBoardCharger){
        .input_good = input_good,
        .charge_enabled = charge_permitted && charge_configured,
        .charge_state = BQ25601_charge_status(),
        .faults = faults, .input_limit_ma = BQ25601_get_input_current_limit_mA(),
    };
    return GTOS_BOARD_OK;
}

static uint16_t available_charge_input(const GtosBoardUsb* usb) {
    if (!usb->sink_attached) return 0;
#ifndef CONFIG_GTOS_A3_SIMULATION
    // CC's Default USB Power mode does not report this device's USB allocation.
    if (usb->advertised_ma < 1500) return 0;
#endif
    return usb->advertised_ma;
}

int gtos_board_read_usb(GtosBoardUsb* usb) {
    if (!usb) return GTOS_BOARD_INVALID;
    if (!cc_ready) return GTOS_BOARD_MISSING;
    uint8_t status[2] = {0};
    const bool read_ok = read_registers(cc, 0x08, status, sizeof(status));
    const bool sink = read_ok && (status[1] >> 6) == 2;
    const uint16_t usb_current_ma[] = {500, 1500, 500, 3000};
    const GtosBoardUsb snapshot = {
        .sink_attached = sink, .flipped = sink && (status[1] & 0x20),
        .advertised_ma = sink ? usb_current_ma[(status[0] >> 4) & 3] : 0,
    };
    const uint16_t available = available_charge_input(&snapshot);
    if (charge_permitted && available < active_charge.input_limit_ma) {
        const int result = gtos_board_set_charge(false);
        if (result != GTOS_BOARD_OK) return result;
        // IINDPM limits the system path as well as charging. Follow a known
        // lower source capability even after dropping the charge gate.
        if (available) {
            bq_io_failure = false;
            if (BQ25601_set_input_current_limit_mA(available) != available || bq_io_failure)
                return GTOS_BOARD_IO;
        }
    }
    if (!read_ok) return GTOS_BOARD_IO;
    *usb = snapshot;
    return GTOS_BOARD_OK;
}

void a3_i2c_poll(uint32_t milliseconds) {
    static uint32_t last_check;
    if (!charge_permitted) return;
    if (!power_good_pin()) {
        gtos_board_set_charge(false);
        return;
    }
    if (milliseconds - last_check < 100) return;
    last_check = milliseconds;
    GtosBoardUsb usb;
    gtos_board_read_usb(&usb);
    if (charge_permitted) {
        GtosBoardCharger charger;
        gtos_board_read_charger(&charger);
    }
}

static A3ChargeProfile charge_profile(void) {
#ifdef CONFIG_GTOS_A3_SIMULATION
    return (A3ChargeProfile){
        .input_limit_ma = 500, .fast_charge_ma = 240, .regulation_mv = 4176,
        .precharge_ma = 60, .termination_ma = 60, .safety_timer_hours = 5,
    };
#elif defined(GTOS_A3_CHARGE_PROFILE)
    return gtos_a3_charge_profile;
#else
    return (A3ChargeProfile){0};
#endif
}

static bool valid_charge_profile(const A3ChargeProfile* profile) {
    return profile->input_limit_ma >= 100 && profile->input_limit_ma <= 3200 && profile->input_limit_ma % 100 == 0
        && profile->fast_charge_ma >= 60 && profile->fast_charge_ma <= 3000 && profile->fast_charge_ma % 60 == 0
        && profile->precharge_ma >= 60 && profile->precharge_ma <= 780 && profile->precharge_ma % 60 == 0
        && profile->termination_ma >= 60 && profile->termination_ma <= 780 && profile->termination_ma % 60 == 0
        && profile->precharge_ma <= profile->fast_charge_ma && profile->termination_ma <= profile->fast_charge_ma
        && profile->regulation_mv >= 3856 && profile->regulation_mv <= 4200
        && (profile->safety_timer_hours == 5 || profile->safety_timer_hours == 10);
}

static int disable_charge(void) {
    if (!charge_gpio_ready || !bq_ready) return GTOS_BOARD_MISSING;
    if (gpio_set_level(A3_PIN_CHARGE_EN, 0) != ESP_OK) return GTOS_BOARD_IO;
    charge_permitted = false;
    bq_io_failure = false;
    BQ25601_set_charge_enabled(false);
    BQ25601_read_register(BQ25601_REG01);
    return !bq_io_failure && !BQ25601_get_charge_enabled() ? GTOS_BOARD_OK : GTOS_BOARD_IO;
}

static int program_charge(A3ChargeProfile* profile) {
    // Enter host mode and keep the programmed limits from reverting on an I2C
    // watchdog timeout. The separate charge safety timer stays enabled.
    bq_io_failure = false;
    BQ25601_set_watchdog_reset(WD_RST_RESET);
    BQ25601_set_watchdog_time(WATCHDOG_DISABLED);
    BQ25601_set_otg_enabled(false);
    BQ25601_set_input_current_limit_mA(profile->input_limit_ma);
    BQ25601_set_fast_charge_current_mA(profile->fast_charge_ma);
    BQ25601_set_pre_charge_current_mA(profile->precharge_ma);
    BQ25601_set_charge_termination_current_mA(profile->termination_ma);
    profile->regulation_mv = BQ25601_set_vreg_mV(profile->regulation_mv);
    BQ25601_set_topoff_timer(TOPOFF_TIMER_DISABLED);
    BQ25601_set_enable_termination(true);
    BQ25601_set_enable_timer(true);
    BQ25601_set_charge_timer(safety_timer(profile));
    BQ25601_set_x2_slow_safety_timer(false);
    if (bq_io_failure || !profile->regulation_mv || !charge_settings_match(profile)) return GTOS_BOARD_IO;

    BQ25601_set_charge_enabled(true);
    uint8_t faults = 0;
    if (bq_io_failure || !read_charger_registers(true, &faults)
        || !BQ25601_get_charge_enabled() || !charge_settings_match(profile))
        return GTOS_BOARD_IO;
    GtosBoardUsb usb;
    if (gtos_board_read_usb(&usb) != GTOS_BOARD_OK) return GTOS_BOARD_IO;
    if (faults || !power_good_pin() || !BQ25601_power_good()
        || available_charge_input(&usb) < profile->input_limit_ma)
        return GTOS_BOARD_NOT_READY;
    return gpio_set_level(A3_PIN_CHARGE_EN, 1) == ESP_OK ? GTOS_BOARD_OK : GTOS_BOARD_IO;
}

int gtos_board_set_charge(bool enabled) {
    int result = disable_charge();
    if (!enabled || result != GTOS_BOARD_OK) return result;

    A3ChargeProfile profile = charge_profile();
    if (!valid_charge_profile(&profile)) return GTOS_BOARD_NOT_READY;
    GtosBoardUsb usb;
    GtosBoardCharger charger;
    if (gtos_board_read_usb(&usb) || gtos_board_read_charger(&charger)) return GTOS_BOARD_IO;
    const uint16_t available = available_charge_input(&usb);
    if (!available || !charger.input_good || (charger.faults & ~WatchdogFault)) return GTOS_BOARD_NOT_READY;
    if (available < profile.input_limit_ma) profile.input_limit_ma = available;

    result = program_charge(&profile);
    if (result != GTOS_BOARD_OK) {
        disable_charge();
        return result;
    }
    active_charge = profile;
    charge_permitted = true;
    return GTOS_BOARD_OK;
}
