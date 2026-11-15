#include <stdio.h>
#include "board_a3.h"
#include "dashboard.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

unsigned gtos_riscv_boot(void* arena, unsigned bytes);

static int read_command(bool console, bool usb, bool* from_usb) {
    uint8_t byte;
    *from_usb = false;
    if (console && uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1) return byte;
    if (usb && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1) {
        *from_usb = true;
        return byte;
    }
    return -1;
}

static bool c6_service(bool console, bool usb) {
    if (!console && !usb) {
        puts("GTOS_A3_C6_SERVICE FAIL console");
        return false;
    }
    if (!a3_c6_power_hold_reset()) {
        a3_c6_power_off();
        puts("GTOS_A3_C6_SERVICE FAIL power");
        return false;
    }
    puts("GTOS_A3_C6_SERVICE RESET_HELD r=reset x=exit");
    for (;;) {
        bool from_usb;
        const int command = read_command(console, usb, &from_usb);
        if (command == 'r') {
            puts(a3_c6_reset() ? "GTOS_A3_C6_SERVICE RESET_RELEASED" : "GTOS_A3_C6_SERVICE FAIL reset");
        } else if (command == 'x') {
            if (a3_c6_power_off()) {
                puts("GTOS_A3_C6_SERVICE EXIT");
                return true;
            }
            puts("GTOS_A3_C6_SERVICE FAIL power off");
        }
        a3_i2c_poll((uint32_t)(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void report_probe(bool usb, unsigned revision, unsigned psram, uint32_t flash, const char* elf) {
#ifdef CONFIG_GTOS_A3_SIMULATION
    const unsigned simulation = 1;
#else
    const unsigned simulation = 0;
#endif
#ifdef GTOS_A3_PANEL_PROFILE
    const unsigned panel_profile = 1;
#else
    const unsigned panel_profile = 0;
#endif
#ifdef GTOS_A3_CHARGE_PROFILE
    const unsigned charge_profile = 1;
#else
    const unsigned charge_profile = 0;
#endif
    char response[480];
    const int prefix = snprintf(response, sizeof(response),
        "GTOS_A3_PROBE v=1 sim=%u panel=%u charging_profile=%u rev=%u psram=%u flash=%lu elf=%s",
        simulation, panel_profile, charge_profile, revision, psram, (unsigned long)flash, elf);
    if (prefix < 0 || (size_t)prefix >= sizeof(response)) return;
    const size_t space = sizeof(response) - prefix;
    const int length = gtos_dashboard_probe(response + prefix, space);
    if (length < 0 || (size_t)length >= space) return;
    if (usb) usb_serial_jtag_write_bytes(response, prefix + length, pdMS_TO_TICKS(30) + 1);
    else uart_write_bytes(UART_NUM_0, response, prefix + length);
}

void app_main(void) {
    const unsigned psram_bytes = esp_psram_get_size();
    const unsigned arena_bytes = 1024 * 1024;
    void* arena = heap_caps_aligned_alloc(16, arena_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_bytes != 16 * 1024 * 1024 || !arena) {
        printf("GTOS_A3_CORE FAIL psram=%u\n", psram_bytes);
        heap_caps_free(arena);
        return;
    }
    const unsigned failed = gtos_riscv_boot(arena, arena_bytes);
    printf("GTOS_A3_CORE %s psram=%u checks=%u\n", failed ? "FAIL" : "PASS", psram_bytes, failed);
    heap_caps_free(arena);
    if (failed) return;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    if (esp_flash_get_physical_size(NULL, &flash_bytes) != ESP_OK) flash_bytes = 0;
    const esp_app_desc_t* app = esp_app_get_description();
    const char hex[] = "0123456789abcdef";
    char elf[sizeof(app->app_elf_sha256) * 2 + 1];
    for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
        const uint8_t byte = app->app_elf_sha256[i];
        elf[i * 2] = hex[byte >> 4];
        elf[i * 2 + 1] = hex[byte & 15];
    }
    elf[sizeof(elf) - 1] = '\0';

    a3_i2c_init();
    a3_input_init();
    a3_display_init();
    const bool console = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK;
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 512, .rx_buffer_size = 256,
    };
    const bool usb = usb_serial_jtag_driver_install(&usb_config) == ESP_OK;
    if (a3_wake_held_at_boot() && !c6_service(console, usb)) return;

    a3_storage_init();
    uint16_t* pixels = heap_caps_malloc(A3_WIDTH * A3_HEIGHT * sizeof(*pixels),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        puts("GTOS_A3_DASHBOARD FAIL no framebuffer");
        return;
    }
    gtos_dashboard_start(pixels);
    printf("GTOS_A3_READY console=%u usb=%u\n", console, usb);
    for (;;) {
        bool usb_command;
        const int command = read_command(console, usb, &usb_command);
        if (usb_command && command == 's') puts("GTOS_A3_USB STATUS REQUEST");
#ifdef CONFIG_GTOS_A3_SIMULATION
        if (command == 'q') {
            gtos_board_set_charge(false);
            gtos_dashboard_stop_status();
            puts("GTOS_A3_VM_STOP");
            heap_caps_free(pixels);
            return;
        }
#endif
        const uint32_t milliseconds = (uint32_t)(esp_timer_get_time() / 1000);
        a3_i2c_poll(milliseconds);
        gtos_dashboard_tick(milliseconds, command);
        if (command == 'i') report_probe(usb_command, chip.revision, psram_bytes, flash_bytes, elf);
        if (command == 'f') a3_display_dump();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
