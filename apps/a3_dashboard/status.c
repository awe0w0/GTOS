#include "dashboard.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static char published[GtosDashboardStatusBytes];
static httpd_handle_t server;
static bool start_attempted;

static esp_err_t get_status(httpd_req_t* request) {
    char snapshot[GtosDashboardStatusBytes];
    portENTER_CRITICAL(&status_lock);
    memcpy(snapshot, published, sizeof(snapshot));
    portEXIT_CRITICAL(&status_lock);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, snapshot);
}

void gtos_dashboard_publish_status(const char* json, bool online) {
    portENTER_CRITICAL(&status_lock);
    memcpy(published, json, strlen(json) + 1);
    portEXIT_CRITICAL(&status_lock);
    if (!online) start_attempted = false;
    if (server || !online || start_attempted) return;
    start_attempted = true;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 1;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    const httpd_uri_t route = {.uri = "/status", .method = HTTP_GET, .handler = get_status};
    if (httpd_start(&server, &config) == ESP_OK) {
        if (httpd_register_uri_handler(server, &route) == ESP_OK) {
            puts("GTOS_A3_ORBIT HTTP READY port=8080");
            return;
        }
        gtos_dashboard_stop_status();
    }
    puts("GTOS_A3_ORBIT HTTP FAIL");
}

void gtos_dashboard_stop_status(void) {
    if (server && httpd_stop(server) == ESP_OK) server = NULL;
}
