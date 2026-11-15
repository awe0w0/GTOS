#ifndef GTOS_DRIVERS_BOARD_H
#define GTOS_DRIVERS_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GtosBoardResult {
    GTOS_BOARD_OK = 0,
    GTOS_BOARD_IO = -5,
    GTOS_BOARD_MISSING = -19,
    GTOS_BOARD_INVALID = -22,
    GTOS_BOARD_NOT_FOUND = -2,
    GTOS_BOARD_NOT_READY = -11,
};

enum GtosBoardKey {
    GTOS_BOARD_WAKE = 1,
    GTOS_BOARD_NEXT = 2,
    GTOS_BOARD_BACK = 4,
    GTOS_BOARD_KNOB = 8,
};

typedef struct {
    int32_t acceleration_mg[3];
    int32_t angular_rate_mdps[3];
} GtosBoardMotion;

typedef struct {
    uint32_t buttons;
    uint32_t pressed;
    int32_t encoder_steps; // Position in detents since startup, clockwise positive.
} GtosBoardInput;

typedef struct {
    bool input_good;
    bool charge_enabled;
    uint8_t charge_state; // 0 idle, 1 precharge, 2 fast charge, 3 complete.
    uint8_t faults;
    uint16_t input_limit_ma;
} GtosBoardCharger;

typedef struct {
    bool sink_attached;
    bool flipped;
    uint16_t advertised_ma; // 0 when detached or the current is undetermined.
} GtosBoardUsb;

typedef struct {
    bool powered;
    bool sdio_ready;
    bool link_failed;
    bool scan_complete;
    bool ble_advertising;
    uint16_t nearby_access_points;
    char first_network[33];
} GtosBoardRadio;

enum GtosBoardWifiState {
    GTOS_BOARD_WIFI_IDLE,
    GTOS_BOARD_WIFI_JOINING,
    GTOS_BOARD_WIFI_ONLINE,
    GTOS_BOARD_WIFI_FAILED,
};

typedef struct {
    enum GtosBoardWifiState state;
    char ipv4[16]; // Set after DHCP assigns a station address.
    unsigned disconnect_reason; // Zero if failure did not come from the radio.
} GtosBoardWifi;

// Board services are owned by the application task. Drivers publish results
// into the provided output buffer only when returning GTOS_BOARD_OK.
int gtos_board_read_motion(GtosBoardMotion* motion);
int gtos_board_read_input(GtosBoardInput* input);
int gtos_board_read_charger(GtosBoardCharger* charger);
int gtos_board_read_usb(GtosBoardUsb* usb);
int gtos_board_set_charge(bool enabled);
int gtos_board_start_radio(void);
int gtos_board_read_radio(GtosBoardRadio* radio);
// Start a WPA2-capable station join after the radio scan. Credentials stay in RAM.
int gtos_board_connect_wifi(const char* ssid, const char* password);
int gtos_board_read_wifi(GtosBoardWifi* wifi);

// Publish one read-only ATT value that fits in a single default-MTU response.
enum { GTOS_BOARD_BLE_STATUS_MAX = 20 };
int gtos_board_publish_ble_status(const void* data, size_t size);
bool gtos_board_ble_connected(void);

// RGB565, row-major. The board reports the dimensions of its visible surface.
void gtos_board_display_size(unsigned* width, unsigned* height);
int gtos_board_set_display(bool awake);
int gtos_board_set_brightness(unsigned percent);
int gtos_board_present(const uint16_t* pixels, size_t count);

// Blobs of 1 to 64 bytes in the board settings namespace, up to 15 bytes per key.
int gtos_board_setting_read(const char* key, void* data, size_t size);
int gtos_board_setting_write(const char* key, const void* data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
