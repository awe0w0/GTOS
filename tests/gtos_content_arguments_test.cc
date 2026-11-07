#include <process/abi.h>
#include <string.h>

namespace {
volatile bool initialized;

void Emit(const char* text, unsigned bytes) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call) : "b"(text), "c"(bytes) : "cc", "memory");
}

struct Initializer {
    Initializer() {
        initialized = true;
        static const char marker[] = "GTOS CONTENT ARGUMENTS INIT\n";
        Emit(marker, sizeof(marker) - 1);
    }
};

Initializer initializer;
}

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    static const char marker[] = "GTOS CONTENT ARGUMENTS MAIN\n";
    Emit(marker, sizeof(marker) - 1);
    if (!initialized) return 5;
    if (!argv || !argv[0] || strcmp(argv[0], "content_shell") != 0) return 2;
    if (argc == 1 && !argv[1]) return 37;
    if (argc == 17 && !argv[17]) {
        for (int i = 1; i < argc; ++i) {
            if (!argv[i]) return 6;
        }
        return strcmp(argv[9], "nine") == 0 && strcmp(argv[10], "ten") == 0 &&
               strcmp(argv[16], "sixteen") == 0 ? 74 : 7;
    }
    if (argc != 4 || !argv[1] || !argv[2] || !argv[3] || argv[4]) return 3;
    if (strcmp(argv[1], "--single-process") != 0 ||
        strcmp(argv[2], "file:///fixtures/page with spaces.html") != 0 || *argv[3]) return 4;
    return 73;
}
