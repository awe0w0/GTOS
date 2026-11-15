#include "dashboard.h"
#include <drivers/board.h>
#include <stdio.h>
#include "sdkconfig.h"

extern const unsigned char* gtos_dashboard_glyph(char character);

enum { Orbit, Power, Radio, PageCount };
enum {
    Night = 0x0843, Panel = 0x1168, Grid = 0x224c, White = 0xefbf,
    Muted = 0x8c76, Mint = 0x57d4, Amber = 0xff08, Blue = 0x6e7f,
};

static struct {
    uint16_t* pixels;
    unsigned width, height, page, brightness;
    bool awake, redraw, wifi_requested, ble_connected;
    int32_t encoder;
    uint32_t last_poll, buttons;
    int input_result, motion_result, charger_result, usb_result;
    GtosBoardMotion motion;
    GtosBoardCharger charger;
    GtosBoardUsb usb;
    GtosBoardRadio radio;
    GtosBoardWifi wifi;
} dashboard;

static int limit(int64_t value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void rect(int x, int y, int width, int height, uint16_t color) {
    for (int row = y; row < y + height; ++row)
        for (int col = x; col < x + width; ++col)
            dashboard.pixels[(size_t)row * dashboard.width + col] = color;
}

static void text(int x, int y, unsigned scale, const char* string, uint16_t color) {
    for (; *string && x + 5 * (int)scale < (int)dashboard.width; ++string, x += 6 * scale) {
        const unsigned char* glyph = gtos_dashboard_glyph(*string);
        if (!glyph) continue;
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned col = 0; col < 5; ++col) {
                if (glyph[row] & (16 >> col))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void dot(int x, int y, int radius, uint16_t color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius)
                dashboard.pixels[(size_t)(y + dy) * dashboard.width + x + dx] = color;
        }
    }
}

static void orbit(void) {
    rect(24, 98, 890, 148, Panel);
    for (int y = 110; y < 240; y += 22)
        for (int x = 41; x < 894; x += 23) rect(x, y, 2, 2, Grid);
    rect(468, 106, 2, 132, Grid);
    rect(34, 172, 870, 2, Grid);
    if (dashboard.motion_result) {
        text(48, 148, 4, "IMU UNAVAILABLE", Amber);
        return;
    }
    const int x = 468 + limit(dashboard.motion.acceleration_mg[0], -1000, 1000) * 2 / 5;
    const int y = 172 + limit(dashboard.motion.acceleration_mg[1], -1000, 1000) / 20;
    const int spin = limit(dashboard.motion.angular_rate_mdps[2] / 10000, -12, 12);
    dot(x, y, 20, Grid);
    dot(x + spin, y, 12, Blue);
    dot(x + spin + 2, y - 3, 6, White);

    rect(940, 98, 460, 148, Panel);
    text(968, 119, 2, "ACCELERATION MG", Muted);
    char axes[64];
    snprintf(axes, sizeof(axes), "%ld / %ld / %ld",
             (long)dashboard.motion.acceleration_mg[0], (long)dashboard.motion.acceleration_mg[1],
             (long)dashboard.motion.acceleration_mg[2]);
    text(968, 143, 3, axes, White);
    snprintf(axes, sizeof(axes), "GYRO %ld MDPS", (long)dashboard.motion.angular_rate_mdps[2]);
    text(968, 197, 2, axes, Blue);
}

static void power(void) {
    rect(24, 98, 676, 148, Panel);
    rect(724, 98, 676, 148, Panel);
    text(52, 119, 2, "USB C INPUT", Muted);
    text(752, 119, 2, "BATTERY CHARGER", Muted);
    if (dashboard.usb_result) {
        text(52, 152, 4, "CC UNAVAILABLE", Amber);
    } else if (dashboard.usb.sink_attached) {
        char value[32];
        snprintf(value, sizeof(value), "%u MA ADVERTISED", dashboard.usb.advertised_ma);
        text(52, 152, 4, value, White);
        text(52, 206, 2, dashboard.usb.flipped ? "PLUG FLIPPED" : "PLUG NORMAL", Mint);
    } else {
        text(52, 152, 4, "DETACHED", Amber);
    }
    if (dashboard.charger_result) {
        text(752, 152, 4, "CHARGER UNAVAILABLE", Amber);
    } else {
        char value[40];
        text(752, 152, 4, dashboard.charger.charge_enabled ? "CHARGE ENABLED" : "CHARGE OFF",
             dashboard.charger.charge_enabled ? Mint : White);
        snprintf(value, sizeof(value), "LIMIT %u MA / FAULT %u",
                 dashboard.charger.input_limit_ma, dashboard.charger.faults);
        text(752, 206, 2, value, Muted);
    }
}

static void wireless(void) {
    rect(24, 98, 676, 148, Panel);
    rect(724, 98, 676, 148, Panel);
    text(52, 119, 2, "WI FI VIA ESP32 C6", Muted);
    text(752, 119, 2, "BLUETOOTH LE", Muted);
    if (dashboard.radio.link_failed) {
        text(52, 152, 4, "C6 UNAVAILABLE", Amber);
    } else if (!dashboard.radio.sdio_ready) {
        text(52, 152, 4, dashboard.radio.powered ? "WAITING FOR SDIO" : "C6 POWER OFF", Amber);
    } else if (!dashboard.radio.scan_complete) {
        text(52, 152, 4, "SCANNING", Amber);
    } else if (dashboard.wifi.state == GTOS_BOARD_WIFI_ONLINE) {
        text(52, 152, 4, "WIFI CONNECTED", Mint);
        text(52, 206, 2, dashboard.wifi.ipv4, White);
    } else if (dashboard.wifi.state == GTOS_BOARD_WIFI_JOINING) {
        text(52, 152, 4, "JOINING WIFI", Amber);
    } else if (dashboard.wifi.state == GTOS_BOARD_WIFI_FAILED) {
        text(52, 152, 4, "WIFI JOIN FAILED", Amber);
    } else {
        char value[32];
        snprintf(value, sizeof(value), "%u ACCESS POINTS", dashboard.radio.nearby_access_points);
        text(52, 152, 4, value, White);
        text(52, 206, 2, dashboard.radio.first_network, Mint);
    }
    const bool ble_ready = dashboard.radio.ble_advertising || dashboard.ble_connected;
    text(752, 152, 4, ble_ready ? "GTOS ORBIT" : "INACTIVE", ble_ready ? Mint : Muted);
    if (ble_ready) text(752, 206, 2, dashboard.ble_connected ? "CLIENT CONNECTED" : "ADVERTISING", Blue);
}

static void select_page(unsigned page) {
    dashboard.page = page;
    if (page == Radio) gtos_board_start_radio();
}

static void render(void) {
    if (!dashboard.awake) return;
    rect(0, 0, dashboard.width, dashboard.height, Night);
    rect(24, 25, 8, 46, Mint);
    const char* titles[] = {"DESKTOP ORBIT", "POWER PATH", "RADIO"};
    text(49, 28, 5, titles[dashboard.page], White);
    char light[32];
    snprintf(light, sizeof(light), "LIGHT %u", dashboard.brightness);
    text(1183, 38, 3, light, Mint);
    if (dashboard.page == Orbit) orbit();
    else if (dashboard.page == Power) power();
    else wireless();
    text(24, 258, 2, "NEXT / BACK   CHANGE VIEW       KNOB   LIGHT       PRESS   SAVE", Muted);
    gtos_board_present(dashboard.pixels, (size_t)dashboard.width * dashboard.height);
}

static void store_u16(uint8_t* dest, uint16_t value) {
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
}

static void publish_ble_status(void) {
    // One fixed-length read at the default ATT MTU. All multibyte fields are little endian.
    const uint8_t flags = (dashboard.awake ? 1 : 0) | (dashboard.motion_result == GTOS_BOARD_OK ? 2 : 0)
        | (dashboard.usb_result == GTOS_BOARD_OK && dashboard.usb.sink_attached ? 4 : 0)
        | (dashboard.charger_result == GTOS_BOARD_OK && dashboard.charger.charge_enabled ? 8 : 0);
    uint8_t value[19] = {1, dashboard.page, dashboard.brightness, flags};
    for (unsigned axis = 0; axis < 3; ++axis)
        store_u16(value + 4 + axis * 2, (uint16_t)dashboard.motion.acceleration_mg[axis]);
    store_u16(value + 10, (uint16_t)(dashboard.motion.angular_rate_mdps[2] / 1000));
    store_u16(value + 12, dashboard.usb_result == GTOS_BOARD_OK ? dashboard.usb.advertised_ma : 0);
    value[14] = dashboard.charger_result == GTOS_BOARD_OK ? dashboard.charger.faults : 0;
    const uint32_t steps = (uint32_t)dashboard.encoder;
    for (unsigned byte = 0; byte < 4; ++byte) value[15 + byte] = (uint8_t)(steps >> (byte * 8));
    gtos_board_publish_ble_status(value, sizeof(value));
}

static void sample(void) {
    dashboard.motion_result = gtos_board_read_motion(&dashboard.motion);
    dashboard.usb_result = gtos_board_read_usb(&dashboard.usb);
    dashboard.charger_result = gtos_board_read_charger(&dashboard.charger);
    gtos_board_read_radio(&dashboard.radio);
    gtos_board_read_wifi(&dashboard.wifi);
    dashboard.ble_connected = gtos_board_ble_connected();
    publish_ble_status();
    if (!CONFIG_GTOS_A3_WIFI_SSID[0]) return;
    if (dashboard.page == Radio && dashboard.radio.scan_complete && !dashboard.wifi_requested) {
        dashboard.wifi_requested = true;
        if (dashboard.wifi.state == GTOS_BOARD_WIFI_IDLE || dashboard.wifi.state == GTOS_BOARD_WIFI_FAILED) {
            const int result = gtos_board_connect_wifi(CONFIG_GTOS_A3_WIFI_SSID, CONFIG_GTOS_A3_WIFI_PASSWORD);
            printf("GTOS_A3_ORBIT WIFI JOIN result=%d\n", result);
            gtos_board_read_wifi(&dashboard.wifi);
        }
    }

    char status[GtosDashboardStatusBytes];
    const bool online = dashboard.wifi.state == GTOS_BOARD_WIFI_ONLINE;
    const int count = snprintf(status, sizeof(status),
             "{\"page\":%u,\"brightness\":%u,\"awake\":%u,\"encoder\":%ld,"
             "\"imu_result\":%d,\"accel_mg\":[%ld,%ld,%ld],\"gyro_mdps\":[%ld,%ld,%ld],"
             "\"usb_result\":%d,\"usb_attached\":%u,\"advertised_ma\":%u,"
             "\"charger_result\":%d,\"charging\":%u,\"charge_faults\":%u,"
             "\"sdio_ready\":%u,\"wifi_online\":%u,\"ble_advertising\":%u}\n",
             dashboard.page, dashboard.brightness, dashboard.awake, (long)dashboard.encoder,
             dashboard.motion_result, (long)dashboard.motion.acceleration_mg[0],
             (long)dashboard.motion.acceleration_mg[1], (long)dashboard.motion.acceleration_mg[2],
             (long)dashboard.motion.angular_rate_mdps[0], (long)dashboard.motion.angular_rate_mdps[1],
             (long)dashboard.motion.angular_rate_mdps[2], dashboard.usb_result,
             dashboard.usb.sink_attached, dashboard.usb.advertised_ma, dashboard.charger_result,
             dashboard.charger.charge_enabled, dashboard.charger.faults, dashboard.radio.sdio_ready,
             online, dashboard.radio.ble_advertising);
    if (count >= 0 && (size_t)count < sizeof(status)) gtos_dashboard_publish_status(status, online);
}

static void report(void) {
    printf("GTOS_A3_VIEW page=%u light=%u awake=%u encoder=%ld\n",
           dashboard.page, dashboard.brightness, dashboard.awake, (long)dashboard.encoder);
    printf("GTOS_A3_DEVICES imu=%d accel=%ld,%ld,%ld gyro=%ld,%ld,%ld "
           "cc=%d sink=%u flipped=%u advertised=%u bq=%d pg=%u charge=%u limit=%u faults=%u\n",
           dashboard.motion_result, (long)dashboard.motion.acceleration_mg[0],
           (long)dashboard.motion.acceleration_mg[1], (long)dashboard.motion.acceleration_mg[2],
           (long)dashboard.motion.angular_rate_mdps[0], (long)dashboard.motion.angular_rate_mdps[1],
           (long)dashboard.motion.angular_rate_mdps[2], dashboard.usb_result,
           dashboard.usb.sink_attached, dashboard.usb.flipped, dashboard.usb.advertised_ma,
           dashboard.charger_result, dashboard.charger.input_good, dashboard.charger.charge_enabled,
           dashboard.charger.input_limit_ma, dashboard.charger.faults);
    if (dashboard.page == Radio) {
        printf("GTOS_A3_WIRELESS powered=%u sdio=%u wifi=%u ble=%u station=%u ip=%s client=%u\n",
               dashboard.radio.powered, dashboard.radio.sdio_ready,
               dashboard.radio.scan_complete, dashboard.radio.ble_advertising,
               (unsigned)dashboard.wifi.state, dashboard.wifi.ipv4, dashboard.ble_connected);
    }
}

int gtos_dashboard_probe(char* buffer, size_t size) {
    return snprintf(buffer, size, " page=%u light=%u awake=%u input=%d keys=%lu encoder=%ld"
                    " imu=%d accel=%ld,%ld,%ld gyro=%ld,%ld,%ld cc=%d sink=%u flipped=%u advertised=%u"
                    " bq=%d pg=%u charge=%u faults=%u radio=%u sdio=%u scan=%u aps=%u ble=%u client=%u\n",
                    dashboard.page, dashboard.brightness, dashboard.awake, dashboard.input_result,
                    (unsigned long)dashboard.buttons, (long)dashboard.encoder, dashboard.motion_result,
                    (long)dashboard.motion.acceleration_mg[0], (long)dashboard.motion.acceleration_mg[1],
                    (long)dashboard.motion.acceleration_mg[2], (long)dashboard.motion.angular_rate_mdps[0],
                    (long)dashboard.motion.angular_rate_mdps[1], (long)dashboard.motion.angular_rate_mdps[2],
                    dashboard.usb_result, dashboard.usb.sink_attached, dashboard.usb.flipped, dashboard.usb.advertised_ma,
                    dashboard.charger_result, dashboard.charger.input_good, dashboard.charger.charge_enabled,
                    dashboard.charger.faults, dashboard.radio.powered, dashboard.radio.sdio_ready,
                    dashboard.radio.scan_complete, dashboard.radio.nearby_access_points,
                    dashboard.radio.ble_advertising, dashboard.ble_connected);
}

void gtos_dashboard_start(uint16_t* pixels) {
    dashboard.pixels = pixels;
    gtos_board_display_size(&dashboard.width, &dashboard.height);
    dashboard.brightness = 60;
    uint8_t preferences[3];
    const int result = gtos_board_setting_read("dashboard", preferences, sizeof(preferences));
    if (result == GTOS_BOARD_OK && preferences[0] == 1
        && preferences[1] < PageCount && preferences[2] <= 100) {
        select_page(preferences[1]);
        dashboard.brightness = preferences[2];
        printf("GTOS_A3_SETTINGS LOAD page=%u light=%u\n", dashboard.page, dashboard.brightness);
    }
    gtos_board_set_brightness(dashboard.brightness);
    dashboard.awake = gtos_board_set_display(true) == GTOS_BOARD_OK;
    sample();
    render();
    report();
}

static void save(void) {
    const uint8_t preferences[] = {1, dashboard.page, dashboard.brightness};
    printf("GTOS_A3_SETTINGS SAVE result=%d page=%u light=%u\n",
           gtos_board_setting_write("dashboard", preferences, sizeof(preferences)),
           dashboard.page, dashboard.brightness);
}

void gtos_dashboard_tick(uint32_t milliseconds, int command) {
    bool changed = false;
    GtosBoardInput input;
    dashboard.input_result = gtos_board_read_input(&input);
    if (dashboard.input_result == GTOS_BOARD_OK) {
        dashboard.buttons = input.buttons;
        if (input.encoder_steps != dashboard.encoder) {
            const int64_t delta = (int64_t)input.encoder_steps - dashboard.encoder;
            const int value = (int)dashboard.brightness + limit(delta, -20, 20) * 5;
            const unsigned brightness = limit(value, 0, 100);
            if (gtos_board_set_brightness(brightness) == GTOS_BOARD_OK)
                dashboard.brightness = brightness;
            dashboard.encoder = input.encoder_steps;
            changed = true;
        }
        if (input.pressed & GTOS_BOARD_WAKE) {
            if (gtos_board_set_display(!dashboard.awake) == GTOS_BOARD_OK)
                dashboard.awake = !dashboard.awake;
            changed = true;
        }
        if (input.pressed & GTOS_BOARD_NEXT) {
            select_page((dashboard.page + 1) % PageCount);
            changed = true;
        }
        if (input.pressed & GTOS_BOARD_BACK) {
            select_page((dashboard.page + PageCount - 1) % PageCount);
            changed = true;
        }
        if (input.pressed & GTOS_BOARD_KNOB) save();
    } else {
        dashboard.buttons = 0;
    }
    if (command == 'c') {
        printf("GTOS_A3_CHARGE result=%d\n", gtos_board_set_charge(!dashboard.charger.charge_enabled));
        changed = true;
    }
    if (command == 'w' && dashboard.page == Radio) {
        GtosBoardRadio radio;
        if (gtos_board_read_radio(&radio) == GTOS_BOARD_OK && radio.link_failed)
            gtos_board_start_radio();
        dashboard.wifi_requested = false;
        changed = true;
    }
    if (changed || command == 's' || milliseconds - dashboard.last_poll >= 120) {
        dashboard.last_poll = milliseconds;
        sample();
        render();
        if (changed || command == 's') report();
    }
}
