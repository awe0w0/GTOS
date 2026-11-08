#include <process/abi.h>
#include <process/file_abi.h>
#include <storage/content_volume_layout.h>

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

} // namespace

extern "C" int GtosContentShellMain(int, const char**) {
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
    return 37;
}
