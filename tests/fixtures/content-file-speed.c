/* CPL3 workload for measuring the disk-backed Content volume through the file ABI. */
#include <process/abi.h>
#include <process/file_abi.h>
#include <process/info_abi.h>

enum { TransferBytes = GTOS_FILE_TRANSFER_LIMIT, BlockCount = 8192 };
enum { PositionedReadCount = 512, PositionedReadBytes = 512 };
static const char input_path[] = "/content/io-seed.bin";
static const char output_path[] = "/content/io-write.bin";
static unsigned received[TransferBytes / sizeof(unsigned)] __attribute__((aligned(TransferBytes)));

static int call(unsigned operation, const void* request, unsigned bytes) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(operation), "b"((unsigned)request), "c"(bytes) : "memory", "cc");
    return result;
}

#define Log(literal) call(GTOS_SYS_WRITE, literal, sizeof(literal) - 1)

static int open_file(const char* path, unsigned bytes, unsigned flags) {
    GtosFileOpenRequest request = {GTOS_FILE_ABI_VERSION, (unsigned)path, bytes, flags};
    return call(GTOS_SYS_FILE_OPEN, &request, sizeof(request));
}

static int control(unsigned operation, int handle) {
    GtosFileControlRequest request = {GTOS_FILE_ABI_VERSION, (unsigned)handle};
    return call(operation, &request, sizeof(request));
}

static int transfer(unsigned operation, int handle, void* buffer) {
    GtosFileTransferRequest request = {
        GTOS_FILE_ABI_VERSION, (unsigned)handle, (unsigned)buffer, TransferBytes
    };
    return call(operation, &request, sizeof(request));
}

static int reject_invalid_read(int handle) {
    unsigned char* last = (unsigned char*)received + TransferBytes - 1;
    *last = 0xA5;
    GtosFileTransferRequest request = {
        GTOS_FILE_ABI_VERSION, (unsigned)handle, (unsigned)last, 2
    };
    if (call(GTOS_SYS_FILE_READ, &request, sizeof(request) - 1) != GTOS_FILE_ERR_INVALID
        || call(GTOS_SYS_FILE_READ, &request, sizeof(request)) != GTOS_FILE_ERR_BAD_ADDRESS
        || *last != 0xA5) return 0;
    request.buffer = (unsigned)input_path;
    if (call(GTOS_SYS_FILE_READ, &request, sizeof(request)) != GTOS_FILE_ERR_BAD_ADDRESS) return 0;

    GtosFileInfo info;
    GtosFileStatRequest stat = {
        GTOS_FILE_ABI_VERSION, (unsigned)input_path, sizeof(input_path),
        (unsigned)input_path, sizeof(info)
    };
    if (call(GTOS_SYS_FILE_STAT, &stat, sizeof(stat)) != GTOS_FILE_ERR_BAD_ADDRESS) return 0;
    stat.result = (unsigned)&info;
    if (call(GTOS_SYS_FILE_STAT, &stat, sizeof(stat)) != 0
        || info.type != GTOS_FILE_TYPE_REGULAR || info.bytes != BlockCount * TransferBytes) return 0;

    const GtosFileSeekRequest cursor = {GTOS_FILE_ABI_VERSION, (unsigned)handle, 0, 0, 1};
    return call(GTOS_SYS_FILE_SEEK, &cursor, sizeof(cursor)) == 0;
}

static int reject_invalid_write(int handle) {
    const GtosFilePositionedTransferRequest request = {
        GTOS_FILE_ABI_VERSION, (unsigned)handle, (unsigned)received + TransferBytes - 1, 2, 512, 0
    };
    return call(GTOS_SYS_FILE_WRITE_AT, &request, sizeof(request)) == GTOS_FILE_ERR_BAD_ADDRESS
        && control(GTOS_SYS_FILE_SIZE, handle) == 0;
}

static int read_and_check(int handle, unsigned* pattern, unsigned tag) {
    if (handle <= 0) return 0;
    for (unsigned block = 0; block < BlockCount; ++block) {
        if (transfer(GTOS_SYS_FILE_READ, handle, received) != TransferBytes) return 0;
        pattern[0] = block ^ tag;
        const unsigned* expected = pattern;
        const unsigned* actual = received;
        unsigned words = TransferBytes / sizeof(unsigned);
        unsigned char matches;
        __asm__ volatile("cld; repe cmpsl; sete %0"
                         : "=q"(matches), "+S"(expected), "+D"(actual), "+c"(words)
                         : : "memory", "cc");
        if (!matches) return 0;
    }
    return transfer(GTOS_SYS_FILE_READ, handle, received) == 0 &&
           control(GTOS_SYS_FILE_CLOSE, handle) == 0;
}

static int read_positioned_and_check(const unsigned* pattern, unsigned tag) {
    int handle = open_file(input_path, sizeof(input_path), GTOS_FILE_READ);
    if (handle <= 0) return 0;
    for (unsigned index = 0; index < PositionedReadCount; ++index) {
        unsigned block = (17 + index * 4051) % BlockCount;
        unsigned byte = (index % (TransferBytes / PositionedReadBytes)) * PositionedReadBytes;
        GtosFilePositionedTransferRequest request = {
            GTOS_FILE_ABI_VERSION, (unsigned)handle, (unsigned)received, PositionedReadBytes,
            block * TransferBytes + byte, 0
        };
        if (call(GTOS_SYS_FILE_READ_AT, &request, sizeof(request)) != PositionedReadBytes) return 0;
        for (unsigned word = 0; word < PositionedReadBytes / sizeof(unsigned); ++word) {
            unsigned position = byte / sizeof(unsigned) + word;
            if (received[word] != (position ? pattern[position] : block ^ tag)) return 0;
        }
    }
    return control(GTOS_SYS_FILE_CLOSE, handle) == 0;
}

#ifdef GTOS_PROCESS_INFO_BENCH_CALLS
static int measure_process_info(void) {
    GtosProcessInfoResult result;
    const GtosProcessInfoRequest request = {
        GTOS_PROCESS_INFO_ABI_VERSION, 0, (unsigned)&result, sizeof(result)
    };
    if (call(GTOS_SYS_PROCESS_INFO, &request, sizeof(request)) != 0) return 0;
    const unsigned process = result.process_id, thread = result.thread_id;
    if (!process || !thread) return 0;
    Log("GTOS PROCESS INFO BENCH BEGIN\n");
    for (unsigned i = 0; i < GTOS_PROCESS_INFO_BENCH_CALLS; ++i) {
        if (call(GTOS_SYS_PROCESS_INFO, &request, sizeof(request)) != 0
            || result.version != GTOS_PROCESS_INFO_ABI_VERSION
            || result.process_id != process || result.thread_id != thread) return 0;
    }
    Log("GTOS PROCESS INFO BENCH END\n");
    return 1;
}
#endif

int browser_probe_main(void) {
#ifdef GTOS_TEST_CHROMIUM_THREAD_ID
    extern int CheckChromiumThreadIdentity(void);
    if (!CheckChromiumThreadIdentity()) return 9;
    Log("GTOS CHROMIUM THREAD ID PASS\n");
#endif
#ifdef GTOS_PROCESS_INFO_BENCH_CALLS
    if (!measure_process_info()) return 8;
#endif
    unsigned pattern[TransferBytes / sizeof(unsigned)];
    for (unsigned word = 1; word < TransferBytes / sizeof(unsigned); ++word)
        pattern[word] = 0x6ca49d81u ^ (word * 0x9e3779b9u);

    Log("GTOS FILE SPEED POSITIONED BEGIN\n");
    if (!read_positioned_and_check(pattern, 0xa23f89d5u)) return 7;
    Log("GTOS FILE SPEED POSITIONED END\n");

    Log("GTOS FILE SPEED READ BEGIN\n");
    int handle = open_file(input_path, sizeof(input_path), GTOS_FILE_READ);
    if (handle <= 0 || !reject_invalid_read(handle)) return 10;
    if (!read_and_check(handle, pattern, 0xa23f89d5u)) return 1;
    Log("GTOS FILE SPEED READ END\n");

    Log("GTOS FILE SPEED WRITE BEGIN\n");
    handle = open_file(output_path, sizeof(output_path),
                       GTOS_FILE_WRITE | GTOS_FILE_CREATE | GTOS_FILE_EXCLUSIVE);
    if (handle <= 0) return 2;
    if (!reject_invalid_write(handle)) return 11;
    for (unsigned block = 0; block < BlockCount; ++block) {
        pattern[0] = block ^ 0x783bcc0du;
        if (transfer(GTOS_SYS_FILE_WRITE, handle, pattern) != TransferBytes) return 3;
    }
    if (control(GTOS_SYS_FILE_SYNC, handle) != 0) return 4;
    if (control(GTOS_SYS_FILE_CLOSE, handle) != 0) return 5;
    Log("GTOS FILE SPEED WRITE END\n");

    handle = open_file(output_path, sizeof(output_path), GTOS_FILE_READ);
    if (!read_and_check(handle, pattern, 0x783bcc0du)) return 6;
    Log("GTOS FILE ABI RANGE AND CURSOR PASS\n");
    Log("GTOS FILE SPEED SYNC AND READBACK PASS\n");
    return 0;
}
