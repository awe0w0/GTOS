#include <process/resources.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using gtos::process::ResourceInfo;
using gtos::process::ResourcePlan;
using gtos::process::ResourceReadPlan;

namespace {
    const unsigned ExpectedBytes = 1108U;
    unsigned char expected[ExpectedBytes];
    unsigned long checks = 0;

    void Check(bool condition, const char* label) {
        ++checks;
        if (!condition) {
            std::fprintf(stderr, "FAIL %s at check %lu\n", label, checks);
            std::exit(1);
        }
    }

    void LoadExpected(const char* path) {
        // This independent PNG is generated from source pixels, not read from
        // the compiled service array or its C byte-literal include.
        std::FILE* input = std::fopen(path, "rb");
        Check(input != 0, "open independent PNG");
        const std::size_t count = std::fread(expected, 1, sizeof(expected), input);
        const int extra = std::fgetc(input);
        Check(count == sizeof(expected) && extra == EOF && !std::ferror(input), "exact independent PNG size");
        Check(std::fclose(input) == 0, "close independent PNG");
        const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        Check(std::memcmp(expected, signature, sizeof(signature)) == 0, "PNG signature");
    }

    GtosResourceReadRequest Request(unsigned offset = 0, unsigned length = 1) {
        const GtosResourceReadRequest request = {
            GTOS_RESOURCE_ABI_VERSION, GTOS_RESOURCE_PUBLIC_PNG_ID,
            offset, 0x40002000U, length
        };
        return request;
    }

    void Error(const GtosResourceReadRequest& request, unsigned size, int error) {
        const unsigned char sentinel = 0xA5;
        ResourceReadPlan plan = {&sentinel, 0xA55A5AA5U};
        Check(ResourcePlan(request, size, plan) == error, "read error classification");
        Check(plan.source == &sentinel && plan.bytes == 0xA55A5AA5U, "read error leaves plan unchanged");
    }

    void Catalog() {
        GtosResourceInfo info = {};
        Check(ResourceInfo(GTOS_RESOURCE_PUBLIC_PNG_ID, info) == 0, "public ID exists");
        Check(info.version == 1 && info.type == 1 && info.bytes == ExpectedBytes &&
              info.flags == (GTOS_RESOURCE_FLAG_READONLY | GTOS_RESOURCE_FLAG_PUBLIC), "exact public info");
        const GtosResourceInfo sentinel = {11, 22, 33, 44};
        for (unsigned id = 0; id <= 65536U; ++id) {
            if (id == GTOS_RESOURCE_PUBLIC_PNG_ID) continue;
            info = sentinel;
            Check(ResourceInfo(id, info) == GTOS_RESOURCE_ERR_NOT_FOUND, "no other catalog ID");
            Check(std::memcmp(&info, &sentinel, sizeof(info)) == 0, "info error leaves output unchanged");
        }
        info = sentinel;
        Check(ResourceInfo(UINT_MAX, info) == GTOS_RESOURCE_ERR_NOT_FOUND, "maximum ID absent");
        Check(std::memcmp(&info, &sentinel, sizeof(info)) == 0, "maximum ID output unchanged");
    }

    void Precedence() {
        GtosResourceReadRequest request = Request(UINT_MAX, UINT_MAX);
        request.version = UINT_MAX;
        request.id = UINT_MAX;
        const unsigned invalidSizes[] = {0, 1, 16, 19, 21, 256, UINT_MAX};
        for (unsigned size : invalidSizes) Error(request, size, GTOS_RESOURCE_ERR_BAD_SIZE);
        Error(request, 20, GTOS_RESOURCE_ERR_UNSUPPORTED_VERSION);
        request.version = 1;
        Error(request, 20, GTOS_RESOURCE_ERR_NOT_FOUND);
        request.id = 1;
        Error(request, 20, GTOS_RESOURCE_ERR_TOO_LARGE);
        request.length = 256;
        Error(request, 20, GTOS_RESOURCE_ERR_RANGE);
        const unsigned invalidVersions[] = {0, 2, 0x80000000U, UINT_MAX};
        for (unsigned version : invalidVersions) {
            request = Request(); request.version = version;
            Error(request, 20, GTOS_RESOURCE_ERR_UNSUPPORTED_VERSION);
        }
        const unsigned invalidIds[] = {0, 2, 0x80000000U, UINT_MAX};
        for (unsigned id : invalidIds) {
            request = Request(); request.id = id;
            Error(request, 20, GTOS_RESOURCE_ERR_NOT_FOUND);
        }
        const unsigned invalidLengths[] = {257, 65536, 0x80000000U, UINT_MAX};
        for (unsigned length : invalidLengths) Error(Request(0, length), 20, GTOS_RESOURCE_ERR_TOO_LARGE);
        const unsigned invalidOffsets[] = {ExpectedBytes + 1, 0x80000000U, UINT_MAX};
        for (unsigned offset : invalidOffsets) {
            Error(Request(offset, 0), 20, GTOS_RESOURCE_ERR_RANGE);
            Error(Request(offset, 256), 20, GTOS_RESOURCE_ERR_RANGE);
        }
    }

    void ExhaustiveReads() {
        unsigned char guarded[GTOS_RESOURCE_READ_LIMIT + 32];
        // Exhaust every in-resource byte offset, EOF and one-past-EOF against
        // every transfer size through one-past-cap. The oracle bounds formula
        // uses widened endpoint arithmetic rather than the service subtraction.
        for (unsigned offset = 0; offset <= ExpectedBytes + 1; ++offset) {
            for (unsigned length = 0; length <= GTOS_RESOURCE_READ_LIMIT + 1; ++length) {
                const GtosResourceReadRequest request = Request(offset, length);
                if (length > 256) { Error(request, 20, GTOS_RESOURCE_ERR_TOO_LARGE); continue; }
                if (offset > ExpectedBytes) { Error(request, 20, GTOS_RESOURCE_ERR_RANGE); continue; }
                const unsigned long long endpoint = static_cast<unsigned long long>(offset) + length;
                const unsigned end = endpoint < ExpectedBytes ? static_cast<unsigned>(endpoint) : ExpectedBytes;
                const unsigned count = end - offset;
                ResourceReadPlan plan = {};
                Check(ResourcePlan(request, 20, plan) == static_cast<int>(count), "oracle read result");
                Check(plan.source != 0 && plan.bytes == count, "bounded read plan");
                std::memset(guarded, 0xD3, sizeof(guarded));
                if (count) std::memcpy(guarded + 16, plan.source, count);
                Check(std::memcmp(guarded + 16, expected + offset, count) == 0, "all planned bytes match independent PNG");
                for (unsigned i = 0; i < 16; ++i) Check(guarded[i] == 0xD3, "leading output canary");
                for (unsigned i = 16 + count; i < sizeof(guarded); ++i) Check(guarded[i] == 0xD3, "trailing output canary");
            }
        }
    }

    void ChunkedAndImmutable() {
        unsigned char assembled[ExpectedBytes];
        for (unsigned chunk = 1; chunk <= GTOS_RESOURCE_READ_LIMIT; ++chunk) {
            std::memset(assembled, 0, sizeof(assembled));
            unsigned offset = 0;
            while (offset < ExpectedBytes) {
                ResourceReadPlan plan = {};
                const GtosResourceReadRequest request = Request(offset, chunk);
                const int result = ResourcePlan(request, 20, plan);
                Check(result > 0 && static_cast<unsigned>(result) == plan.bytes, "chunk progress");
                std::memcpy(assembled + offset, plan.source, plan.bytes);
                offset += plan.bytes;
            }
            Check(std::memcmp(assembled, expected, sizeof(assembled)) == 0, "complete chunked retrieval");
        }
        ResourceReadPlan first = {}, again = {};
        Check(ResourcePlan(Request(0, 256), 20, first) == 256, "first immutable read");
        unsigned char copy[256];
        std::memcpy(copy, first.source, sizeof(copy));
        std::memset(copy, 0, sizeof(copy));
        Check(ResourcePlan(Request(0, 256), 20, again) == 256, "repeated immutable read");
        Check(std::memcmp(again.source, expected, 256) == 0, "mutating copy cannot alter resource");
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: native-resource-host INDEPENDENT.png\n");
        return 2;
    }
    LoadExpected(argv[1]);
    Catalog();
    Precedence();
    ExhaustiveReads();
    ChunkedAndImmutable();
    std::printf("NATIVE RESOURCE HOST PASS checks=%lu payload_bytes=%u exhaustive_offsets=%u lengths=258\n",
                checks, ExpectedBytes, ExpectedBytes + 2);
    return 0;
}
