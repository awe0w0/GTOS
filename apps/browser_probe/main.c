/* Integer-only application fixture for the provisional GTOS ABI v1.
 * This probes real guest services; no libc, Linux calls or browser API stubs.
 * Not a Chromium build and not executable on the currently published baseline.
 */
#include <process/vm_abi.h>

typedef unsigned int u32;
typedef int s32;

static s32 syscall2(u32 number, u32 arg1, u32 arg2) {
    s32 result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(arg1), "c"(arg2) : "memory", "cc");
    return result;
}

static s32 write_message(const char* text, u32 length) {
    return syscall2(0x4701, (u32)text, length);
}

static volatile u32 data_marker = 0x47544f53;
static volatile u32 bss_marker;

static int probe_vm(void) {
    const u32 bytes = 2 * GTOS_VM_PAGE_BYTES;
    /* Straddle a 2 MiB PAE directory boundary. */
    GtosVmReserveResult result = {0};
    GtosVmReserveRequest request = {GTOS_VM_ABI_VERSION, bytes, GTOS_VM_PAGE_BYTES,
                                  0x801ff000u, (u32)&result};
    if (syscall2(GTOS_SYS_VM_RESERVE, (u32)&request, sizeof(request)) != 0 ||
        result.base != request.hint || result.length != bytes ||
        result.page_size != GTOS_VM_PAGE_BYTES || !result.handle) return 0;
    GtosVmRangeRequest range = {GTOS_VM_ABI_VERSION, result.handle, 0, bytes, GTOS_VM_READ_WRITE};
    if (syscall2(GTOS_SYS_VM_SET_PERMISSIONS, (u32)&range, sizeof(range)) != 0) return 0;
    volatile char* memory = (volatile char*)result.base;
    for (u32 i = 0; i < bytes; ++i) if (memory[i]) return 0;
    const u32 edge = GTOS_VM_PAGE_BYTES - 8;
    volatile GtosVmRegionInfo* region = (volatile GtosVmRegionInfo*)(result.base + edge);
    GtosVmQueryRequest query = {GTOS_VM_ABI_VERSION, result.handle, (u32)region};
    if (syscall2(GTOS_SYS_VM_QUERY, (u32)&query, sizeof(query)) != 0 ||
        region->handle != result.handle || region->base != result.base ||
        region->resident_pages != 2) return 0;
    static const char message[] = "BROWSER PLATFORM PROBE VM COPY PASS\n";
    for (u32 i = 0; i < sizeof(message) - 1; ++i) memory[edge + i] = message[i];
    if (write_message((const char*)(result.base + edge), sizeof(message) - 1) < 0) return 0;
    range.protection = GTOS_VM_NONE;
    if (syscall2(GTOS_SYS_VM_DISCARD, (u32)&range, sizeof(range)) != 0) return 0;
    for (u32 i = 0; i < bytes; ++i) if (memory[i]) return 0;
    memory[0] = 1; memory[GTOS_VM_PAGE_BYTES] = 2;
    range.length = GTOS_VM_PAGE_BYTES;
    if (syscall2(GTOS_SYS_VM_DECOMMIT, (u32)&range, sizeof(range)) != 0 ||
        write_message((const char*)result.base, 1) != -14) return 0;
    range.protection = GTOS_VM_READ_WRITE;
    if (syscall2(GTOS_SYS_VM_SET_PERMISSIONS, (u32)&range, sizeof(range)) != 0 ||
        memory[0] != 0 || memory[GTOS_VM_PAGE_BYTES] != 2) return 0;
    /* Leave both committed pages to exercise process teardown. */
    return 1;
}

int browser_probe_main(void) {
    static const char begin[] = "BROWSER PLATFORM PROBE BEGIN ABI1\n";
    static const char pass[] = "BROWSER PLATFORM PROBE PASS ABI1\n";
    static const char fail[] = "BROWSER PLATFORM PROBE FAIL ABI1\n";
    if (syscall2(0x4700, 0, 0) != 1) return 10;
    if (write_message(begin, sizeof(begin) - 1) < 0) return 11;
    if (data_marker != 0x47544f53 || bss_marker != 0) goto bad;
    bss_marker = 7;
    if (syscall2(0x4701, 0, 1) != -14) goto bad;
    if (syscall2(0x4701, (u32)begin, 257) != -7) goto bad;
    if (syscall2(0x47ff, 0, 0) != -38) goto bad;
    {
        u32 before = (u32)syscall2(0x4702, 0, 0);
        for (u32 i = 0; i < 64; ++i) syscall2(0x4703, 0, 0);
        u32 after = (u32)syscall2(0x4702, 0, 0);
        /* Wrap-safe ordering only; this does not prove time advanced. */
        if ((s32)(after - before) < 0 || bss_marker != 7) goto bad;
    }
    if (!probe_vm()) goto bad;
    if (write_message(pass, sizeof(pass) - 1) < 0) return 12;
    return 0;
bad:
    write_message(fail, sizeof(fail) - 1);
    return 20;
}
