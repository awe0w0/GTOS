#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"

unsigned gtos_riscv_boot(void* arena, unsigned bytes);

void app_main(void) {
    const unsigned psram_bytes = esp_psram_get_size();
    const unsigned arena_bytes = 1024 * 1024;
    void* arena = heap_caps_aligned_alloc(16, arena_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_bytes != 16 * 1024 * 1024 || !arena) {
        printf("GTOS_A3_CORE FAIL psram=%u\n", psram_bytes);
    } else {
        unsigned failed = gtos_riscv_boot(arena, arena_bytes);
        printf("GTOS_A3_CORE %s psram=%u checks=%u\n", failed ? "FAIL" : "PASS",
               psram_bytes, failed);
    }
    heap_caps_free(arena);
}
