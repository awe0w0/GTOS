#include "board_a3.h"
#include "driver/gpio.h"
#include "eh_host_port_power.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_bt_host_stack.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <stdio.h>
#include <string.h>

static portMUX_TYPE radio_lock = portMUX_INITIALIZER_UNLOCKED;
static GtosBoardRadio radio_status;
static GtosBoardWifi wifi_status;
static int64_t wifi_join_deadline;
static bool started;

// The Q1 transistor inverts the A3 P4-side reset output relative to C6 EN.
eh_host_port_err_t __wrap_eh_host_port_reset_slave(void) {
    if (gpio_set_level(A3_PIN_C6_RESET_ASSERT, 1) != ESP_OK) return EH_HOST_PORT_ERR;
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    if (gpio_set_level(A3_PIN_C6_RESET_ASSERT, 0) != ESP_OK) return EH_HOST_PORT_ERR;
    vTaskDelay(pdMS_TO_TICKS(250));
    return EH_HOST_PORT_OK;
}

static bool power_on(void) {
    const unsigned rf = A3_PIN_RF_ON, reset = A3_PIN_C6_RESET_ASSERT;
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << rf) | (1ULL << reset), .mode = GPIO_MODE_OUTPUT,
    };
    if (gpio_set_level(rf, 0) != ESP_OK || gpio_set_level(reset, 1) != ESP_OK
        || gpio_config(&outputs) != ESP_OK || gpio_set_level(rf, 1) != ESP_OK)
        return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (gpio_set_level(reset, 0) != ESP_OK) {
        gpio_set_level(rf, 0);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    return true;
}

static void wifi_event(void* unused, esp_event_base_t base, int32_t id, void* data) {
    (void)unused;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t* event = data;
        GtosBoardWifi online = {.state = GTOS_BOARD_WIFI_ONLINE};
        snprintf(online.ipv4, sizeof(online.ipv4), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&radio_lock);
        const bool connected = wifi_status.state == GTOS_BOARD_WIFI_JOINING
            || wifi_status.state == GTOS_BOARD_WIFI_ONLINE;
        if (connected) wifi_status = online;
        portEXIT_CRITICAL(&radio_lock);
        if (connected) printf("GTOS_A3_RADIO WIFI ONLINE ip=%s\n", online.ipv4);
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
               || (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        const unsigned reason = base == WIFI_EVENT ? ((wifi_event_sta_disconnected_t*)data)->reason : 0;
        portENTER_CRITICAL(&radio_lock);
        const bool was_active = wifi_status.state == GTOS_BOARD_WIFI_JOINING
            || wifi_status.state == GTOS_BOARD_WIFI_ONLINE;
        if (was_active)
            wifi_status = (GtosBoardWifi){.state = GTOS_BOARD_WIFI_FAILED, .disconnect_reason = reason};
        portEXIT_CRITICAL(&radio_lock);
        if (was_active) printf("GTOS_A3_RADIO WIFI OFFLINE reason=%u\n", reason);
    }
}

static void scan(void) {
    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK
        || !esp_netif_create_default_wifi_sta() || esp_wifi_init(&config) != ESP_OK
        || esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL) != ESP_OK
        || esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL) != ESP_OK
        || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK
        || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK
        || esp_wifi_scan_start(NULL, true) != ESP_OK) {
        puts("GTOS_A3_RADIO WIFI FAIL");
        return;
    }
    uint16_t count = 0, returned = 1;
    wifi_ap_record_t first = {};
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK
        || (count && esp_wifi_scan_get_ap_records(&returned, &first) != ESP_OK)) {
        esp_wifi_clear_ap_list();
        puts("GTOS_A3_RADIO WIFI FAIL results");
        return;
    }
    portENTER_CRITICAL(&radio_lock);
    radio_status.scan_complete = true;
    radio_status.nearby_access_points = count;
    memcpy(radio_status.first_network, first.ssid, sizeof(radio_status.first_network) - 1);
    portEXIT_CRITICAL(&radio_lock);
    printf("GTOS_A3_RADIO WIFI PASS count=%u ssid=%.*s\n", count, 32, (const char*)first.ssid);
}

static void advertise(void) {
    const char name[] = "GTOS Orbit";
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t*)name;
    fields.name_len = sizeof(name) - 1;
    fields.name_is_complete = 1;
    const struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON, .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    uint8_t address_type;
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &address_type)
        || ble_gap_adv_set_fields(&fields)
        || ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, NULL, NULL)) {
        puts("GTOS_A3_RADIO BLE FAIL advertising");
        return;
    }
    portENTER_CRITICAL(&radio_lock);
    radio_status.ble_advertising = true;
    portEXIT_CRITICAL(&radio_lock);
    puts("GTOS_A3_RADIO BLE PASS advertising");
}

static void ble_task(void* unused) {
    (void)unused;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void start_ble(void) {
    esp_hosted_bt_host_stack_cfg_t config = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    if (esp_hosted_bt_host_stack_setup(&config) != ESP_OK || nimble_port_init() != ESP_OK) {
        puts("GTOS_A3_RADIO BLE FAIL initialization");
        return;
    }
    ble_hs_cfg.sync_cb = advertise;
    nimble_port_freertos_init(ble_task);
}

static void radio_task(void* unused) {
    (void)unused;
    bool powered = power_on();
    portENTER_CRITICAL(&radio_lock);
    radio_status.powered = powered;
    portEXIT_CRITICAL(&radio_lock);
    esp_hosted_coprocessor_fwver_t version;
    const bool initialized = powered && esp_hosted_init() == ESP_OK;
    if (initialized && esp_hosted_connect_to_slave() == ESP_OK
        && esp_hosted_get_coprocessor_fwversion(&version) == ESP_OK) {
        portENTER_CRITICAL(&radio_lock);
        radio_status.sdio_ready = true;
        portEXIT_CRITICAL(&radio_lock);
        puts("GTOS_A3_RADIO SDIO PASS C6 firmware RPC");
        scan();
        start_ble();
    } else {
        if (initialized) esp_hosted_deinit();
        gpio_set_level(A3_PIN_C6_RESET_ASSERT, 1);
        gpio_set_level(A3_PIN_RF_ON, 0);
        portENTER_CRITICAL(&radio_lock);
        radio_status.powered = false;
        radio_status.link_failed = true;
        started = false;
        portEXIT_CRITICAL(&radio_lock);
        puts("GTOS_A3_RADIO SDIO FAIL");
    }
    vTaskDelete(NULL);
}

int gtos_board_start_radio(void) {
    portENTER_CRITICAL(&radio_lock);
    const bool running = started;
    if (!running) {
        started = true;
        radio_status.link_failed = false;
    }
    portEXIT_CRITICAL(&radio_lock);
    if (running || xTaskCreate(radio_task, "gtos-a3-radio", 6144, NULL, 4, NULL) == pdPASS)
        return GTOS_BOARD_OK;
    portENTER_CRITICAL(&radio_lock);
    started = false;
    radio_status.link_failed = true;
    portEXIT_CRITICAL(&radio_lock);
    return GTOS_BOARD_IO;
}

int gtos_board_read_radio(GtosBoardRadio* radio) {
    if (!radio) return GTOS_BOARD_INVALID;
    portENTER_CRITICAL(&radio_lock);
    *radio = radio_status;
    portEXIT_CRITICAL(&radio_lock);
    return GTOS_BOARD_OK;
}

int gtos_board_connect_wifi(const char* ssid, const char* password) {
    wifi_config_t config = {};
    const size_t ssid_length = ssid ? strnlen(ssid, sizeof(config.sta.ssid) + 1) : 0;
    const size_t password_length = password ? strnlen(password, sizeof(config.sta.password)) : 0;
    if (!ssid_length || ssid_length > sizeof(config.sta.ssid)
        || password_length < 8 || password_length == sizeof(config.sta.password))
        return GTOS_BOARD_INVALID;
    memcpy(config.sta.ssid, ssid, ssid_length);
    memcpy(config.sta.password, password, password_length);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    const int64_t deadline = esp_timer_get_time() + 20 * 1000000;
    portENTER_CRITICAL(&radio_lock);
    const bool ready = radio_status.scan_complete && wifi_status.state != GTOS_BOARD_WIFI_JOINING
        && wifi_status.state != GTOS_BOARD_WIFI_ONLINE;
    if (ready) {
        wifi_status = (GtosBoardWifi){.state = GTOS_BOARD_WIFI_JOINING};
        wifi_join_deadline = deadline;
    }
    portEXIT_CRITICAL(&radio_lock);
    if (!ready) return GTOS_BOARD_NOT_READY;
    if (esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK)
        return GTOS_BOARD_OK;
    portENTER_CRITICAL(&radio_lock);
    wifi_status = (GtosBoardWifi){.state = GTOS_BOARD_WIFI_FAILED};
    portEXIT_CRITICAL(&radio_lock);
    return GTOS_BOARD_IO;
}

int gtos_board_read_wifi(GtosBoardWifi* wifi) {
    if (!wifi) return GTOS_BOARD_INVALID;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&radio_lock);
    const bool expired = wifi_status.state == GTOS_BOARD_WIFI_JOINING && now >= wifi_join_deadline;
    if (expired) wifi_status = (GtosBoardWifi){.state = GTOS_BOARD_WIFI_FAILED};
    *wifi = wifi_status;
    portEXIT_CRITICAL(&radio_lock);
    if (expired) {
        esp_wifi_disconnect();
        puts("GTOS_A3_RADIO WIFI OFFLINE timeout");
    }
    return GTOS_BOARD_OK;
}
