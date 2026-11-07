#include <process/abi.h>
#include <stdlib.h>

#include "config/app.h"

#if defined(GTOS_CRT_BYPASS_ENVIRONMENT)
extern "C" int __wrap_gtos_native_environment_initialize() noexcept {
    return 0;
}
#endif

namespace {
volatile unsigned initialized;

struct Initializer {
    Initializer() {
        const auto* environment = LIBC_NAMESPACE::app.env_ptr;
        if (environment && !environment[0] && !getenv("PATH"))
            initialized = 0x47544f53U;
    }
    ~Initializer() {
        static const char text[] = "GTOS CRT DESTRUCTOR\n";
        unsigned call = GTOS_SYS_WRITE;
        asm volatile("int $0x80" : "+a"(call)
                     : "b"(text), "c"(sizeof(text) - 1) : "cc", "memory");
    }
};

Initializer initializer;
}

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    if (initialized != 0x47544f53U || argc != 1 || !argv || !argv[0] || argv[1]) {
        return 1;
    }
    static const char expected[] = "content_shell";
    for (unsigned i = 0; i < sizeof(expected); ++i) {
        if (argv[0][i] != expected[i]) return 2;
    }
    return 37;
}
