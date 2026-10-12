#include <stdexcept>
#include "counts.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "diagnostic-output.h"
#include "record.h"

extern "C" {
volatile NativeDiagRecord native_diag_record
    __attribute__((section(".data.diag_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,{0,0,0,0}};
}
namespace {
char text[901];
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
int Print(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = gtos_native_vprint_error(format, arguments);
    va_end(arguments);
    return result;
}
}
extern "C" void NativeEntry() {
    Check(v8::base::gtos::CurrentProcessInfo().maximum_parallel_threads == 1, 1);
    GtosVmReserveResult result = {};
    GtosVmReserveRequest reserve = {1, 4096, 4096, 0x80000000U, (unsigned)&result};
    Check(Call(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0, 2);
    Check(result.version == 1 && result.handle && result.base == 0x80000000U
        && result.length == 4096 && result.page_size == 4096, 3);
    GtosVmRangeRequest commit = {1, result.handle, 0, 4096, GTOS_VM_READ_WRITE};
    Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 4);
    Check(*(volatile unsigned*)result.base == 0, 5);
    *(volatile unsigned*)result.base = 0xC011CA11U;
    native_diag_record.region = result.handle;
    native_diag_record.base = result.base;
    native_diag_record.length = result.length;
    for (unsigned i = 0; i < 900; ++i) text[i] = 'A' + i % 26;
    text[900] = 0;
    const unsigned mode = native_diag_record.mode;
    native_diag_record.stage = 1;
    if (mode == 1) {
        native_diag_record.textBytes = 900;
        native_diag_record.expectedBytes = RuntimePrefixBytes + 900;
        std::__throw_runtime_error(text);
    }
    if (mode == 2) {
        native_diag_record.expectedBytes = RuntimePrefixBytes;
        std::__throw_runtime_error("");
    }
    if (mode == 3) {
        native_diag_record.textBytes = ConsumerMessageBytes;
        native_diag_record.expectedBytes = RuntimePrefixBytes + ConsumerMessageBytes;
        std::__throw_runtime_error("locale not supported");
    }
    if (mode == 4) std::__throw_runtime_error((const char*)0xBFFFCFF0U);
    native_diag_record.textBytes = 900;
    native_diag_record.expectedBytes = sizeof("DIAG MESSAGE mode=0 value=-7 hex=00001234 [] END\n") - 1 + 900;
    Check(mode == 0, 6);
    int bytes = Print("DIAG MESSAGE mode=%u value=%d hex=%08x [%s] END\n", mode, -7, 0x1234U, text);
    Check(bytes == (int)native_diag_record.expectedBytes, 7);
    native_diag_record.formattedBytes = bytes;
    Check(Print("") == 0, 8);
    native_diag_record.stage = 2;
    Exit(0);
}
