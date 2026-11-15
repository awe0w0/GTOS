#include <stdio.h>
#include "board_a3.h"
#include "dashboard.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

unsigned gtos_riscv_boot(void* arena, unsigned bytes);

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

    a3_i2c_init();
    a3_input_init();
    a3_storage_init();
    a3_display_init();
    uint16_t* pixels = heap_caps_malloc(A3_WIDTH * A3_HEIGHT * sizeof(*pixels),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        puts("GTOS_A3_DASHBOARD FAIL no framebuffer");
        return;
    }
    const bool console = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK;
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 256, .rx_buffer_size = 256,
    };
    const bool usb = usb_serial_jtag_driver_install(&usb_config) == ESP_OK;
    gtos_dashboard_start(pixels);
    printf("GTOS_A3_READY console=%u usb=%u\n", console, usb);
    for (;;) {
        uint8_t byte;
        int command = console && uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1 ? byte : -1;
        if (command < 0 && usb && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1) {
            command = byte;
            if (command == 's') puts("GTOS_A3_USB STATUS REQUEST");
        }
#ifdef CONFIG_GTOS_A3_SIMULATION
        if (command == 'q') {
            gtos_board_set_charge(false);
            puts("GTOS_A3_VM_STOP");
            heap_caps_free(pixels);
            return;
        }
#endif
        const uint32_t milliseconds = (uint32_t)(esp_timer_get_time() / 1000);
        a3_i2c_poll(milliseconds);
        gtos_dashboard_tick(milliseconds, command);
        if (command == 'f') a3_display_dump();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
