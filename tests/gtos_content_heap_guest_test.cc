// Exercise the native C and C++ allocation entry points used by Content Shell.
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include <process/abi.h>

extern "C" int gtos_content_heap_initialize() noexcept;

namespace {

void Say(const char* message, size_t bytes) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call) : "b"(message), "c"(bytes) : "cc", "memory");
}

int* startup_allocation = static_cast<int*>(std::malloc(sizeof(int)));

constexpr int kBlocks = 20 * 1024;
std::array<unsigned*, kBlocks> blocks;

int CheckHeap() {
    if (!startup_allocation || gtos_content_heap_initialize() != EBUSY) return 201;
    std::free(startup_allocation);
    for (int i = 0; i < kBlocks; ++i) {
        auto* block = new (std::nothrow) unsigned[8];
        if (!block || reinterpret_cast<uintptr_t>(block) % alignof(std::max_align_t)) return 202;
        block[0] = i;
        block[7] = ~static_cast<unsigned>(i);
        blocks[i] = block;
    }
    for (int i = 0; i < kBlocks; ++i) {
        if (blocks[i][0] != static_cast<unsigned>(i) ||
            blocks[i][7] != ~static_cast<unsigned>(i)) return 203;
        delete[] blocks[i];
    }
    void* aligned = nullptr;
    if (posix_memalign(&aligned, 4096, 103) || !aligned ||
        reinterpret_cast<uintptr_t>(aligned) % 4096) return 204;
    std::free(aligned);
    auto* bytes = static_cast<unsigned char*>(std::calloc(4096, 16));
    if (!bytes) return 205;
    for (size_t i = 0; i < 65536; ++i) {
        if (bytes[i]) return 206;
        bytes[i] = static_cast<unsigned char>(i ^ 0x5f);
    }
    constexpr size_t too_large = SIZE_MAX;
    errno = 0;
    if (std::realloc(bytes, too_large) || errno != ENOMEM) return 207;
    auto* moved = static_cast<unsigned char*>(std::realloc(bytes, 131073));
    if (!moved) return 208;
    for (size_t i = 0; i < 65536; ++i) {
        if (moved[i] != static_cast<unsigned char>(i ^ 0x5f)) return 209;
    }
    std::free(moved);
    errno = 0;
    if (std::calloc(too_large, 2) || errno != ENOMEM) return 210;
    void* unchanged = &blocks[0];
    const auto bad_alignment = static_cast<size_t>(3);
    if (posix_memalign(&unchanged, bad_alignment, 32) != EINVAL ||
        unchanged != &blocks[0]) return 211;
    return 37;
}

}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
    const int result = CheckHeap();
    if (result == 37) {
        static constexpr char message[] = "GTOS CONTENT HEAP LIVE ALLOCATION PASS\n";
        Say(message, sizeof(message) - 1);
    }
    return result;
}
