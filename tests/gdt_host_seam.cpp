// GTOS-DSH-138 H: the single definition of the GDT host machine boundary.
//
// This translation unit exists so that every unit which links src/gdt.cpp under GTOS_NATIVE_RUNTIME_HOST_TEST has exactly
// one definition of the nine symbols that boundary needs. In 137 the definitions sat inside safe_tss_test.cpp, so the
// runtime unit — which also links gdt.cpp — could not link at all. Moving them here is the closure, and it is not a
// weakening: the storage is real, the window is real, and the window test that decides what counts as illegal is still
// gdt.cpp's own SafeReadByte.
//
// It deliberately defines no behaviour of its own. A seam that decided anything would be a second implementation of the
// thing under test.

#include <common/types.h>

extern "C" {
uint32_t gtos_gdt_host_str_selector = 0;
uint32_t gtos_gdt_host_sgdt_base = 0;
uint32_t gtos_gdt_host_sgdt_limit = 0;
uint32_t gtos_gdt_host_reads = 0;
uint32_t gtos_gdt_host_window_base = 0;
uint32_t gtos_gdt_host_window_bytes = 0;
uint32_t gtos_gdt_host_illegal_reads = 0;
uint32_t gtos_gdt_host_descriptor_reads = 0;
uint32_t gtos_gdt_host_tss_reads = 0;
}
