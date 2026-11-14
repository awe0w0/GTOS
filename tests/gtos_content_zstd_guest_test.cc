#include <array>
#include <process/abi.h>
#include "third_party/zstd/src/lib/zstd.h"

extern "C" int GtosContentShellMain(int, const char**) {
    std::array<char, 8192> input;
    std::array<char, 8192> output;
    std::array<char, 8300> compressed;
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>('a' + (i / 47) % 23);
    }

    const size_t size = ZSTD_compress(compressed.data(), compressed.size(),
                                      input.data(), input.size(), 3);
    if (ZSTD_isError(size) || size < 4 || size >= input.size()) return 201;
    if (ZSTD_decompress(output.data(), output.size(), compressed.data(), size) !=
            input.size() || output != input) return 202;
    if (!ZSTD_isError(ZSTD_decompress(output.data(), output.size(),
                                      compressed.data(), size - 1))) return 203;
    if (!ZSTD_isError(ZSTD_decompress(output.data(), output.size() - 1,
                                      compressed.data(), size))) return 204;
    const char passed[] = "GTOS ZSTD ROUND TRIP AND REJECTION PASS\n";
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call) : "b"(passed), "c"(sizeof(passed) - 1)
                 : "cc", "memory");
    return 37;
}
