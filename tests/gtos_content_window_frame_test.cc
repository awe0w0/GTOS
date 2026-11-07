#include <process/abi.h>
#include <process/native_window_adapter.h>
#include <process/native_window_frame.h>

namespace {
using gtos::process::NativeWindowAdapter;
using gtos::process::PresentNativeWindowFrame;

void Write(const char* text, unsigned length) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call) : "b"(text), "c"(length) : "cc", "memory");
}

struct ExitMarker {
    ~ExitMarker() {
        static const char text[] = "GTOS CRT DESTRUCTOR\n";
        Write(text, sizeof(text) - 1);
    }
};
ExitMarker exit_marker;

bool State(NativeWindowAdapter& window, unsigned generation, unsigned progress) {
    GtosWindowInfoResponse info{};
    return window.Info(&info) == 0 && info.frame_generation == generation &&
           info.progress == progress;
}
} // namespace

extern "C" int GtosContentShellMain(int, const char**) {
    NativeWindowAdapter window;
    constexpr unsigned width = 19;
    constexpr unsigned height = 5;
    unsigned char pixels[width * height * 4]{};
    for (unsigned offset = 0; offset < sizeof(pixels); offset += 4) {
        pixels[offset] = 0x40;
        pixels[offset + 1] = 0x20;
        pixels[offset + 2] = 0x10;
        pixels[offset + 3] = 0x80;
    }
    const int handle = window.Create(width, height);
    if (handle <= 0) return 1;
    if (PresentNativeWindowFrame(handle, pixels, sizeof(pixels) - 4) !=
        GTOS_WINDOW_ERR_BAD_SIZE || !State(window, 0, 0)) return 2;
    if (PresentNativeWindowFrame(handle, nullptr, sizeof(pixels)) !=
        GTOS_WINDOW_ERR_BAD_ADDRESS || !State(window, 0, 0)) return 3;
    if (PresentNativeWindowFrame(handle, pixels, sizeof(pixels)) != 0 ||
        !State(window, 1, 0)) return 4;
    pixels[0] = 0x20;
    if (PresentNativeWindowFrame(handle, pixels, sizeof(pixels)) != 0 ||
        !State(window, 2, 0)) return 5;
    pixels[GTOS_WINDOW_WRITE_LIMIT] = 0xff;
    if (PresentNativeWindowFrame(handle, pixels, sizeof(pixels)) !=
        GTOS_WINDOW_ERR_BAD_PIXELS || !State(window, 2, GTOS_WINDOW_WRITE_LIMIT)) return 6;
    pixels[GTOS_WINDOW_WRITE_LIMIT] = 0x40;
    if (PresentNativeWindowFrame(handle, pixels, sizeof(pixels)) !=
        GTOS_WINDOW_ERR_BAD_STATE || !State(window, 2, GTOS_WINDOW_WRITE_LIMIT)) return 7;
    if (window.Close() != 0 ||
        PresentNativeWindowFrame(handle, pixels, sizeof(pixels)) !=
            GTOS_WINDOW_ERR_BAD_HANDLE) return 8;
    if (window.Create(width, height) <= handle ||
        PresentNativeWindowFrame(window.handle(), pixels, sizeof(pixels)) != 0 ||
        !State(window, 1, 0) || window.Close() != 0) return 9;
    static const char pass[] = "GTOS CONTENT WINDOW FRAME PASS\n";
    Write(pass, sizeof(pass) - 1);
    return 37;
}
