#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <process/abi.h>
#include "heap.h"
#include "record.h"

extern "C" {
volatile NativeDiagRecord native_diag_record
    __attribute__((section(".data.diag_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,{0,0,0,0}};
}
namespace {
unsigned Call(unsigned number, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(number)
        : "b"((unsigned)request), "c"(bytes) : "memory", "cc");
    return number;
}
[[noreturn]] void Exit(unsigned result) {
    Call(GTOS_SYS_EXIT, (const void*)result, 0);
    __builtin_trap();
}
void Check(bool value, unsigned error) {
    native_diag_record.checks = native_diag_record.checks + 1;
    if (!value) { native_diag_record.error = error; Exit(99); }
}
bool EmptyHeap() {
    GtosVmRegionInfo info = {};
    return gtos_native_heap_query(&info) == 0;
}
#include "sscanf-cases-v1.h"
char longInput[503];
}
extern "C" void NativeEntry() {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest reserve = {1, 4096, 4096, 0x80000000U, (unsigned)&result};
    Check(Call(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0, 1);
    Check(result.version == 1 && result.handle && result.base == 0x80000000U
        && result.length == 4096 && result.page_size == 4096, 2);
    GtosVmRangeRequest commit = {1, result.handle, 0, 4096, GTOS_VM_READ_WRITE};
    Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 3);
    Check(*(volatile unsigned*)result.base == 0, 4);
    *(volatile unsigned*)result.base = 0xC011CA11U;
    Check(EmptyHeap(), 5);
    native_diag_record.region = result.handle;
    native_diag_record.base = result.base;
    native_diag_record.length = result.length;
    native_diag_record.stage = 1;
    const unsigned mode = native_diag_record.mode;
    int integer = 91;
    if (mode == 2) sscanf("1", "%d", (int*)0xBFFFCFF0U);
    if (mode == 3) sscanf((const char*)0xBFFFCFF0U, "%d", &integer);
    if (mode == 4) sscanf("1", (const char*)0xBFFFCFF0U, &integer);
    longInput[0] = '1'; longInput[1] = '.';
    for (unsigned i = 2; i < 502; ++i) longInput[i] = '0';
    longInput[502] = 0;
    long double number = 9.0L;
    if (mode == 1) {
        void* held = malloc(65000);
        Check(held != nullptr, 10);
        errno = 0;
        Check(sscanf(longInput, "%Lf", &number) == 0 && number == 9.0L && errno == ENOMEM, 11);
        free(held);
        Check(EmptyHeap(), 12);
        Check(sscanf("7.25", "%Lf", &number) == 1 && number == 7.25L && EmptyHeap(), 13);
        native_diag_record.textBytes = 1;
    } else if (mode == 5) {
        void* held = malloc(128);
        GtosVmRegionInfo info = {};
        Check(held != nullptr && gtos_native_heap_query(&info) == 1
            && info.length == GTOS_NATIVE_HEAP_BYTES, 14);
        native_diag_record.reserved[0] = info.base;
        for (;;) {
            Check(sscanf(longInput, "%Lf", &number) == 1 && number == 1.0L, 15);
            native_diag_record.progress = native_diag_record.progress + 1;
        }
    } else {
        Check(mode == 0, 20);
        RunScanCases();
        native_diag_record.textBytes = 41;
        char* edge = (char*)(result.base + 4094);
        edge[0] = '1'; edge[1] = 0;
        char next = '?';
        Check(sscanf(edge, "%Lf%c", &number, &next) == 1 && number == 1.0L && next == '?', 21);
        Check(sscanf(edge + 1, "%d", &integer) == -1 && integer == 91, 22);
        Check(sscanf("1", edge + 1) == 0, 23);
        Check(sscanf("%", "%%") == 0, 24);
        Check(EmptyHeap(), 25);
    }
    native_diag_record.stage = 2;
    Exit(0);
}
