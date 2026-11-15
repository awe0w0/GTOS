#include "board_a3.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static bool ready;
static const char Settings[] = "gtos_board";

void a3_storage_init(void) {
    ready = nvs_flash_init() == ESP_OK;
}

static int result(esp_err_t error) {
    if (error == ESP_OK) return GTOS_BOARD_OK;
    if (error == ESP_ERR_NVS_NOT_FOUND) return GTOS_BOARD_NOT_FOUND;
    if (error == ESP_ERR_NVS_INVALID_LENGTH) return GTOS_BOARD_INVALID;
    return GTOS_BOARD_IO;
}

static bool valid(const char* key, const void* bytes, size_t size) {
    return key && key[0] && strnlen(key, 16) < 16 && bytes && size > 0 && size <= 64;
}

int gtos_board_setting_read(const char* key, void* data, size_t size) {
    if (!valid(key, data, size)) return GTOS_BOARD_INVALID;
    if (!ready) return GTOS_BOARD_MISSING;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(Settings, NVS_READONLY, &handle);
    if (error != ESP_OK) return result(error);
    uint8_t stored[64];
    size_t bytes = size;
    error = nvs_get_blob(handle, key, stored, &bytes);
    nvs_close(handle);
    if (error != ESP_OK) return result(error);
    if (bytes != size) return GTOS_BOARD_INVALID;
    memcpy(data, stored, size);
    return GTOS_BOARD_OK;
}

int gtos_board_setting_write(const char* key, const void* data, size_t size) {
    if (!valid(key, data, size)) return GTOS_BOARD_INVALID;
    if (!ready) return GTOS_BOARD_MISSING;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(Settings, NVS_READWRITE, &handle);
    if (error != ESP_OK) return result(error);
    error = nvs_set_blob(handle, key, data, size);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return result(error);
}
