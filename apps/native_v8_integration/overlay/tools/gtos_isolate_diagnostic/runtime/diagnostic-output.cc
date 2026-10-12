#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || GTOS_ISOLATE_COMPILE_DIAGNOSTIC != 1
#error "GTOS diagnostic output requires the native IA32 diagnostic profile"
#endif

#include "diagnostic-output.h"
#include "src/base/platform/gtos-native-api/abi.h"
#include "src/__support/arg_list.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/__support/printf_core/error_mapper.h"
#include "src/__support/printf_core/printf_main.h"
#include "src/__support/printf_core/writer.h"

namespace {
int WriteError(__llvm_libc_cr::cpp::string_view text, void*) {
    const char* data = text.data();
    size_t remaining = text.size();
    while (remaining != 0) {
        unsigned bytes = remaining > GTOS_NATIVE_WRITE_LIMIT
                             ? GTOS_NATIVE_WRITE_LIMIT
                             : static_cast<unsigned>(remaining);
        unsigned result = GTOS_SYS_WRITE;
        asm volatile("int $0x80" : "+a"(result)
                     : "b"(reinterpret_cast<unsigned>(data)), "c"(bytes)
                     : "memory", "cc");
        if (result == 0 || result > bytes)
            return __llvm_libc_cr::printf_core::FILE_WRITE_ERROR;
        data += result;
        remaining -= result;
    }
    return __llvm_libc_cr::printf_core::WRITE_OK;
}
}

int gtos_native_vprint_error(const char* format, va_list arguments) noexcept {
    char buffer[GTOS_NATIVE_WRITE_LIMIT];
    auto writer = __llvm_libc_cr::printf_core::make_writer(
        buffer, sizeof(buffer),
        __llvm_libc_cr::printf_core::overflow_write_flush_to_sink<char, WriteError>);
    __llvm_libc_cr::internal::ArgList args(arguments);
    auto result = __llvm_libc_cr::printf_core::printf_main(&writer, format, args);
    // Flush a valid prefix after conversion failure, but never retry a sink
    // that may already have written a prefix before reporting an I/O error.
    int flush = __llvm_libc_cr::printf_core::WRITE_OK;
    if (result.has_value() || result.error() != -__llvm_libc_cr::printf_core::FILE_WRITE_ERROR)
        flush = writer.get_write_buffer().flush_to_sink<WriteError>();
    if (!result.has_value() || flush < 0) {
        int error = result.has_value() ? -flush : result.error();
        __llvm_libc_cr::libc_errno = __llvm_libc_cr::printf_core::internal_error_to_errno(error);
        return -1;
    }
    if (result.value() > static_cast<size_t>(__llvm_libc_cr::cpp::numeric_limits<int>::max())) {
        __llvm_libc_cr::libc_errno = __llvm_libc_cr::printf_core::internal_error_to_errno(
            -__llvm_libc_cr::printf_core::OVERFLOW_ERROR);
        return -1;
    }
    return static_cast<int>(result.value());
}
