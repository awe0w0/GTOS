#include <process/abi.h>
#include <process/file_abi.h>
#include <storage/content_volume_layout.h>
#include <string.h>

#if defined(GTOS_CONTENT_TFLITE_FILE_TEST)
#include <fcntl.h>
#include <unistd.h>

#include "tensorflow/compiler/mlir/lite/core/model_builder_base.h"
#include "tensorflow/lite/delegates/serialization.h"
#endif
#if defined(GTOS_CONTENT_TFLITE_PLATFORM_TEST)
int TestTflitePlatform();
#endif
#if defined(GTOS_CONTENT_BASE_RANDOM_TEST)
int TestBaseRandom();
#endif
#if defined(GTOS_CONTENT_WEBRTC_AUDIO_TEST)
int TestWebRtcAudioIsa();
#endif

namespace {

template <class Request>
int Call(unsigned operation, const Request& request) {
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(&request), "c"(sizeof(request)) : "cc", "memory");
    return static_cast<int>(operation);
}

template <unsigned Size>
void Say(const char (&text)[Size]) {
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(text), "c"(Size - 1) : "cc", "memory");
}

template <unsigned Size>
int Open(const char (&path)[Size], unsigned flags) {
    const GtosFileOpenRequest request{
        GTOS_FILE_ABI_VERSION, reinterpret_cast<unsigned>(path), Size, flags};
    return Call(GTOS_SYS_FILE_OPEN, request);
}

int Close(unsigned handle) {
    const GtosFileControlRequest request{GTOS_FILE_ABI_VERSION, handle};
    return Call(GTOS_SYS_FILE_CLOSE, request);
}

template <class Byte, unsigned Size>
bool ReadMatches(unsigned handle, unsigned offset, const Byte (&expected)[Size]) {
    unsigned char output[Size];
    const GtosFilePositionedTransferRequest request{
        GTOS_FILE_ABI_VERSION, handle, reinterpret_cast<unsigned>(output),
        Size, offset, 0};
    if (Call(GTOS_SYS_FILE_READ_AT, request) != static_cast<int>(Size)) return false;
    for (unsigned i = 0; i < Size; ++i)
        if (output[i] != static_cast<unsigned char>(expected[i])) return false;
    return true;
}

struct ExitMarker {
    ~ExitMarker() { Say("GTOS CRT DESTRUCTOR\n"); }
};
ExitMarker exit_marker;

#if defined(GTOS_CONTENT_TFLITE_FILE_TEST)
class ModelReporter final : public tflite::ErrorReporter {
public:
    int Report(const char* format, va_list) override {
        last_error = format;
        ++errors;
        return 0;
    }
    const char* last_error = nullptr;
    unsigned errors = 0;
};

int TestModelCopies() {
    if (tflite::MMAPAllocation::IsSupported()) return 6;
    ModelReporter reporter;
    const auto page = tflite::GetAllocationFromFile(
        GTOS_CONTENT_ASSET_DIRECTORY "/index.html", &reporter);
    if (!page || !page->valid() || page->type() != tflite::Allocation::Type::kFileCopy ||
        page->bytes() != 1214 || memcmp(page->base(), "<!doctype", 9) != 0 ||
        reporter.errors) return 7;

    const int fd = open(GTOS_CONTENT_ASSET_DIRECTORY "/bear-vp9.webm", O_RDONLY);
    if (fd < 0) return 8;
    const bool seek = lseek(fd, 27, SEEK_SET) == 27;
    const auto model = tflite::GetAllocationFromFile(fd, &reporter);
    const bool unchanged = lseek(fd, 0, SEEK_CUR) == 27;
    const bool closed = close(fd) == 0;
    if (!seek || !unchanged || !closed || reporter.errors || !model ||
        !model->valid() || model->type() != tflite::Allocation::Type::kFileCopy ||
        model->bytes() != 67504) return 8;
    const auto* bytes = static_cast<const unsigned char*>(model->base());
    unsigned hash = 2166136261u;
    for (size_t i = 0; i < model->bytes(); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    if (hash != 0x8b897321u) return 8;

    // The published ICU data exceeds the current native heap allocation limit.
    const int large_fd = open(GTOS_CONTENT_ASSET_DIRECTORY "/icudtl.dat", O_RDONLY);
    if (large_fd < 0) return 9;
    const bool large_seek = lseek(large_fd, 27, SEEK_SET) == 27;
    const auto large = tflite::GetAllocationFromFile(large_fd, &reporter);
    const bool large_unchanged = lseek(large_fd, 0, SEEK_CUR) == 27;
    const bool large_closed = close(large_fd) == 0;
    if (!large_seek || !large_unchanged || !large_closed || !large ||
        large->valid() || large->bytes() || reporter.errors != 1 ||
        strcmp(reporter.last_error, "Could not allocate model copy.") != 0) return 9;
    Say("GTOS TFLITE FILE HEAP LIMIT PASS\n");

    const auto missing = tflite::GetAllocationFromFile(
        GTOS_CONTENT_ASSET_DIRECTORY "/missing", &reporter);
    const auto invalid = tflite::GetAllocationFromFile(-1, &reporter);
    if (!missing || missing->valid() || missing->bytes() || !invalid ||
        invalid->valid() || invalid->bytes() || reporter.errors != 3) return 10;

    const tflite::delegates::SerializationParams params{
        "model", GTOS_CONTENT_ASSET_DIRECTORY};
    tflite::delegates::Serialization serialization(params);
    const auto cache = serialization.GetEntryForDelegate("fixture", nullptr);
    std::string cache_data = "stale";
    if (cache.SetData(nullptr, "x", 1) != kTfLiteDelegateDataWriteError ||
        cache.GetData(nullptr, &cache_data) != kTfLiteDelegateDataNotFound ||
        !cache_data.empty()) return 11;
    return 0;
}
#endif

} // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    if (argc != 8 || argv[8] || strcmp(argv[6], "--js-flags=--jitless") != 0 ||
        strcmp(argv[7], "file://" GTOS_CONTENT_ASSET_DIRECTORY "/index.html") != 0)
        return 5;
    Say("GTOS CONTENT INTERPRETER ARG PASS\n");
    constexpr char icu[] = GTOS_CONTENT_ASSET_DIRECTORY "/icudtl.dat";
    const int handle = Open(icu, GTOS_FILE_READ);
    if (handle == GTOS_FILE_ERR_UNAVAILABLE) {
        Say("GTOS CONTENT ASSETS UNAVAILABLE\n");
        return 38;
    }
    if (handle <= 0) return 1;

    const GtosFileControlRequest size{GTOS_FILE_ABI_VERSION,
                                      static_cast<unsigned>(handle)};
    constexpr unsigned char start[] = {
        0x90, 0x00, 0xda, 0x27, 0x14, 0, 0, 0, 0, 0, 2, 0, 0x43, 0x6d, 0x6e, 0x44};
    constexpr unsigned char beyond_megabyte[] = {
        0xb8, 0x1d, 0x8a, 0x30, 6, 0, 0x8b, 0x30, 0x73, 0x80, 0x31, 0, 0x53, 0x30, 0x44, 0x30};
    constexpr unsigned char tail[] = {
        0xcf, 0xee, 0x30, 0xc2, 0, 0, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
    if (Call(GTOS_SYS_FILE_SIZE, size) != 10822000 ||
        !ReadMatches(handle, 0, start) ||
        !ReadMatches(handle, 1048576, beyond_megabyte) ||
        !ReadMatches(handle, 10822000 - sizeof(tail), tail) || Close(handle)) return 2;

    const int page = Open(GTOS_CONTENT_ASSET_DIRECTORY "/index.html", GTOS_FILE_READ);
    constexpr unsigned char prefix[] = {'<', '!', 'd', 'o', 'c', 't', 'y', 'p', 'e'};
    if (page <= 0 || !ReadMatches(page, 0, prefix) || Close(page)) return 3;

    // No format or write is allowed from a consumer of this boot volume.
    constexpr int kReadOnly = -30;
    if (Open(icu, GTOS_FILE_WRITE) != kReadOnly ||
        Open(GTOS_CONTENT_ASSET_DIRECTORY "/missing", GTOS_FILE_READ) != -2 ||
        Open(GTOS_CONTENT_ASSET_DIRECTORY "/new",
             GTOS_FILE_WRITE | GTOS_FILE_CREATE) != kReadOnly) return 4;
    Say("GTOS CONTENT ASSETS READONLY PASS\n");
#if defined(GTOS_CONTENT_TFLITE_FILE_TEST)
    if (const int status = TestModelCopies()) return status;
    Say("GTOS TFLITE FILE COPY PASS\n");
#endif
#if defined(GTOS_CONTENT_TFLITE_PLATFORM_TEST)
    if (const int status = TestTflitePlatform()) return status;
    Say("GTOS TFLITE PLATFORM PASS\n");
#endif
#if defined(GTOS_CONTENT_BASE_RANDOM_TEST)
    if (const int status = TestBaseRandom()) return status;
    Say("GTOS BASE RANDOM PASS\n");
#endif
#if defined(GTOS_CONTENT_WEBRTC_AUDIO_TEST)
    if (const int status = TestWebRtcAudioIsa()) return status;
    Say("GTOS WEBRTC AUDIO ISA PASS\n");
#endif
    return 37;
}
