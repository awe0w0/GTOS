#include "esp_err.h"
#include "nvs_flash.h"
#include <stdio.h>

void app_main(void) {
    const esp_err_t result = nvs_flash_init();
    printf("GTOS_A3_C6 %s nvs=%s\n", result == ESP_OK ? "READY" : "FAIL", esp_err_to_name(result));
}
