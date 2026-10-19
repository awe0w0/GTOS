// Bounded host test for the native window bank. Pure state only: real checked
// copy, desktop composition and GUI closing require actual guest acceptance.
#include <process/native_windows.h>

#ifndef GTOS_WINDOW_I386_HOST
#include <cstdio>
#include <cstring>
#define WINLOG std::printf
#define WINMEMCPY std::memcpy
#define WINMEMCMP std::memcmp
#define WINMEMSET std::memset
#else
// Freestanding Linux ELF32 host: no multilib CRT, so the few library routines the
// test uses are supplied here and output goes through int 0x80 write.
extern "C" void* memset(void* out, int value, unsigned length) {
    unsigned char* bytes = static_cast<unsigned char*>(out);
    for (unsigned i = 0; i < length; ++i) bytes[i] = static_cast<unsigned char>(value);
    return out;
}
extern "C" void* memcpy(void* out, const void* in, unsigned length) {
    unsigned char* to = static_cast<unsigned char*>(out);
    const unsigned char* from = static_cast<const unsigned char*>(in);
    for (unsigned i = 0; i < length; ++i) to[i] = from[i];
    return out;
}
extern "C" int memcmp(const void* left, const void* right, unsigned length) {
    const unsigned char* a = static_cast<const unsigned char*>(left);
    const unsigned char* b = static_cast<const unsigned char*>(right);
    for (unsigned i = 0; i < length; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
// Abstract base vtables reference this; a freestanding link has no libstdc++.
extern "C" void __cxa_pure_virtual() { for (;;) {} }
#define WINMEMCPY memcpy
#define WINMEMCMP memcmp
#define WINMEMSET memset
namespace {
void RawWrite(const char* text, unsigned length) {
    int ignored;
    asm volatile("int $0x80"
                 : "=a"(ignored)
                 : "0"(4), "b"(1), "c"(text), "d"(length)
                 : "memory", "cc");
}
void WriteNumber(long value) {
    char buffer[12];
    unsigned end = sizeof(buffer) - 1;
    buffer[end] = 0;
    bool negative = value < 0;
    unsigned magnitude = negative ? (unsigned)(-value) : (unsigned)value;
    do {
        buffer[--end] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (negative) buffer[--end] = '-';
    RawWrite(buffer + end, (unsigned)(sizeof(buffer) - 1 - end));
}
void WriteText(const char* text) {
    unsigned length = 0;
    while (text[length]) ++length;
    RawWrite(text, length);
}
// Minimal %s/%u/%d support; any other conversion is printed verbatim.
void WinLog(const char* format, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, format);
    const char* cursor = format;
    while (*cursor) {
        if (*cursor != '%') { RawWrite(cursor, 1); ++cursor; continue; }
        ++cursor;
        if (*cursor == 's') { WriteText(__builtin_va_arg(args, const char*)); ++cursor; }
        else if (*cursor == 'u') { WriteNumber((long)__builtin_va_arg(args, unsigned)); ++cursor; }
        else if (*cursor == 'd') { WriteNumber((long)__builtin_va_arg(args, int)); ++cursor; }
        else { RawWrite("%", 1); }
    }
    __builtin_va_end(args);
}
}
#define WINLOG WinLog
#endif

using namespace gtos;
using namespace gtos::process;


// Host-only access to production data for boundary positioning. No user ABI is
// added for tests; this is the same pattern the surface host test uses.
namespace gtos { namespace process {
struct NativeWindowHostAccess {
    static void SetNextHandle(NativeWindows& bank, unsigned value) {
        bank.nextHandle = value;
    }
    static unsigned NextHandle(const NativeWindows& bank) { return bank.nextHandle; }
    static void SetFrameGeneration(NativeWindows& bank, unsigned handle, unsigned value) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) bank.slots[i].frameGeneration = value;
    }
    static unsigned Dropped(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].dropped;
        return 0;
    }
    static unsigned QueueCount(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].queueCount;
        return 0;
    }
    static bool ResetPending(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].resetPending;
        return false;
    }
    static bool ClosePending(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].closePending;
        return false;
    }
    static const unsigned char* Front(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].front;
        return 0;
    }
    static const unsigned char* Draft(const NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) return bank.slots[i].draft;
        return 0;
    }
    // Positions the production dropped counter near its ceiling so the real
    // DropNormal path is what performs the saturation.
    static void SetDropped(NativeWindows& bank, unsigned handle, unsigned value) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle) bank.slots[i].dropped = value;
    }
    static void FillQueue(NativeWindows& bank, unsigned handle) {
        for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
            if (bank.slots[i].handle == handle)
                bank.slots[i].queueCount = GTOS_INPUT_NORMAL_ENTRIES;
    }
};
} }  // namespace gtos::process

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        WINLOG("FAIL %s\n", what);
    }
}

// Host memory standing in for user pages. Range checks use subtraction so an
// overflowing address+length cannot wrap past them.
struct Arena : UserMemory {
    unsigned char bytes[65536];
    unsigned readableLimit, writableLimit;
    Arena() : readableLimit(sizeof(bytes)), writableLimit(sizeof(bytes)) {
        WINMEMSET(bytes, 0, sizeof(bytes));
    }
    bool CopyFromUser(void* destination, unsigned source, unsigned length) {
        if (source > readableLimit || length > readableLimit - source) return false;
        WINMEMCPY(destination, bytes + source, length);
        return true;
    }
    bool CopyToUser(unsigned destination, const void* source, unsigned length) {
        if (destination > writableLimit || length > writableLimit - destination)
            return false;
        WINMEMCPY(bytes + destination, source, length);
        return true;
    }
};

NativeWindows bank;
Arena arena;

GtosWindowCreateRequest CreateReq(unsigned w, unsigned h) {
    GtosWindowCreateRequest r;
    r.version = GTOS_WINDOW_ABI_VERSION;
    r.width = w;
    r.height = h;
    r.stride = w * 4;
    r.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    return r;
}

GtosWindowControlRequest Control(unsigned handle) {
    GtosWindowControlRequest r;
    r.version = GTOS_WINDOW_ABI_VERSION;
    r.handle = handle;
    return r;
}

GtosInputEvent Event(unsigned type, unsigned code, int x, int y) {
    GtosInputEvent e;
    e.type = type;
    e.code = code;
    e.x = x;
    e.y = y;
    return e;
}

GtosWindowInfoResponse InfoOf(unsigned owner, unsigned handle, int& result) {
    GtosWindowInfoRequest r;
    r.version = GTOS_WINDOW_ABI_VERSION;
    r.handle = handle;
    r.result_va = 0;
    GtosWindowInfoResponse out;
    result = bank.Info(owner, r, out);
    return out;
}

// Writes the remaining bytes of one frame from the current progress.
void FillFrame(unsigned owner, unsigned handle, unsigned total, unsigned char v) {
    int result = 0;
    unsigned start = InfoOf(owner, handle, result).progress;
    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    for (unsigned offset = start; offset < total; offset += GTOS_WINDOW_WRITE_LIMIT) {
        unsigned length = total - offset;
        if (length > GTOS_WINDOW_WRITE_LIMIT) length = GTOS_WINDOW_WRITE_LIMIT;
        for (unsigned i = 0; i < length; i += 4) {
            chunk[i] = v; chunk[i + 1] = v; chunk[i + 2] = v; chunk[i + 3] = 255;
        }
        GtosWindowSubmitRequest r;
        r.version = GTOS_WINDOW_ABI_VERSION;
        r.handle = handle;
        r.offset = offset;
        r.source = 0;
        r.length = length;
        Check(bank.Submit(owner, r, chunk) == (int)length, "submit chunk");
    }
}

// Places one request record at a fixed arena offset for Call().
unsigned PutRequest(const void* record, unsigned size) {
    WINMEMCPY(arena.bytes + 64, record, size);
    return 64;
}

void WireLayout() {
    Check(sizeof(GtosWindowCreateRequest) == GTOS_WINDOW_CREATE_REQUEST_BYTES, "create size");
    Check(sizeof(GtosWindowSubmitRequest) == GTOS_WINDOW_SUBMIT_REQUEST_BYTES, "submit size");
    Check(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES, "control size");
    Check(sizeof(GtosWindowInfoRequest) == GTOS_WINDOW_INFO_REQUEST_BYTES, "info request size");
    Check(sizeof(GtosInputReadRequest) == GTOS_INPUT_READ_REQUEST_BYTES, "input read size");
    Check(sizeof(GtosWindowInfoResponse) == GTOS_WINDOW_INFO_RESPONSE_BYTES, "info response size");
    Check(sizeof(GtosInputEvent) == GTOS_INPUT_EVENT_BYTES, "event size");
    Check(GTOS_INPUT_NORMAL_ENTRIES + 2 == GTOS_INPUT_STORAGE_ENTRIES, "queue partition");
    WINLOG("wire create=%u submit=%u control=%u info=%u read=%u response=%u event=%u\n",
                (unsigned)sizeof(GtosWindowCreateRequest),
                (unsigned)sizeof(GtosWindowSubmitRequest),
                (unsigned)sizeof(GtosWindowControlRequest),
                (unsigned)sizeof(GtosWindowInfoRequest),
                (unsigned)sizeof(GtosInputReadRequest),
                (unsigned)sizeof(GtosWindowInfoResponse),
                (unsigned)sizeof(GtosInputEvent));
}

void FourOwnersFillSlots() {
    bank.SetAvailable(true);
    // Four distinct owners take the four slots.
    int handles[4];
    for (unsigned owner = 1; owner <= 4; ++owner) {
        handles[owner - 1] = bank.Create(owner, CreateReq(2, 2));
        Check(handles[owner - 1] > 0, "four owners create");
    }
    // A fifth owner is refused by capacity, not by handle exhaustion.
    Check(bank.Create(5, CreateReq(2, 2)) == GTOS_WINDOW_ERR_LIMIT, "fifth owner refused");
    // Per-owner limit is a separate gate: owner 1 already holds one.
    Check(bank.Create(1, CreateReq(2, 2)) == GTOS_WINDOW_ERR_LIMIT, "second object refused");
    // Foreign owner cannot touch another's handle.
    Check(bank.Present(2, Control((unsigned)handles[0])) == GTOS_WINDOW_ERR_BAD_HANDLE,
          "foreign owner rejected");
    for (unsigned i = 0; i < 4; ++i)
        Check(bank.Close(i + 1, Control((unsigned)handles[i])) == 0, "close each owner");
    // Handles never come back.
    int fresh = bank.Create(1, CreateReq(2, 2));
    Check(fresh > handles[3], "handle never reuses");
    bank.Close(1, Control((unsigned)fresh));
    // Stale handle stays dead.
    Check(bank.Present(1, Control((unsigned)handles[0])) == GTOS_WINDOW_ERR_BAD_HANDLE,
          "stale handle rejected");
}

void HandleAndGenerationExhaustion() {
    int handle = bank.Create(11, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    // Push the next handle to the last positive value, use it, then exhaust.
    NativeWindowHostAccess::SetNextHandle(bank, 0x7fffffffU);
    Check(bank.Close(11, Control(h)) == 0, "close before exhaustion probe");
    int last = bank.Create(11, CreateReq(2, 2));
    Check(last == 0x7fffffff, "last positive handle succeeds");
    bank.Close(11, Control((unsigned)last));
    Check(bank.Create(11, CreateReq(2, 2)) == GTOS_WINDOW_ERR_HANDLE_EXHAUSTED,
          "handle exhaustion is its own error");
    // Frame generation exhaustion is independent of the handle. Restore a usable
    // handle counter first so the two gates are probed separately.
    NativeWindowHostAccess::SetNextHandle(bank, 1000);
    int other = bank.Create(11, CreateReq(2, 2));
    Check(other > 0, "create after restoring the counter");
    NativeWindowHostAccess::SetFrameGeneration(bank, (unsigned)other, 0xffffffffU);
    FillFrame(11, (unsigned)other, 16, 5);
    Check(bank.Present(11, Control((unsigned)other)) == GTOS_WINDOW_ERR_OVERFLOW,
          "generation exhaustion rejected");
    int result = 0;
    Check(InfoOf(11, (unsigned)other, result).frame_generation == 0xffffffffU,
          "generation unchanged after refusal");
    bank.Close(11, Control((unsigned)other));
}

void FramesAndPresent() {
    int handle = bank.Create(21, CreateReq(4, 4));
    unsigned h = (unsigned)handle;
    int result = 0;
    GtosWindowInfoResponse info = InfoOf(21, h, result);
    Check(result == 0 && info.show == GTOS_WINDOW_SHOW_EMPTY, "empty show");
    Check(info.frame_generation == 0, "empty generation");
    Check(bank.Present(21, Control(h)) == GTOS_WINDOW_ERR_BAD_STATE, "present without draft");

    // A partial draft is DRAFTING and cannot present.
    unsigned char pixel[4] = {9, 9, 9, 255};
    GtosWindowSubmitRequest sr;
    sr.version = GTOS_WINDOW_ABI_VERSION;
    sr.handle = h;
    sr.offset = 0;
    sr.source = 0;
    sr.length = 4;
    Check(bank.Submit(21, sr, pixel) == 4, "partial submit");
    info = InfoOf(21, h, result);
    Check(result == 0 && info.show == GTOS_WINDOW_SHOW_DRAFTING, "partial draft shows drafting");
    Check(bank.Present(21, Control(h)) == GTOS_WINDOW_ERR_BAD_STATE, "partial present");

    // Non-sequential offset is refused and leaves draft bytes untouched.
    const unsigned char* draftBefore = NativeWindowHostAccess::Draft(bank, h);
    unsigned char snapshot[16];
    WINMEMCPY(snapshot, draftBefore, sizeof(snapshot));
    sr.offset = 0;
    Check(bank.Submit(21, sr, pixel) == GTOS_WINDOW_ERR_BAD_STATE, "restart refused");
    Check(WINMEMCMP(snapshot, NativeWindowHostAccess::Draft(bank, h), sizeof(snapshot)) == 0,
          "draft bytes unchanged after refused restart");
    info = InfoOf(21, h, result);
    Check(info.progress == 4, "progress unchanged");

    // A COMPLETE draft still reports DRAFTING before publication.
    FillFrame(21, h, 64, 7);
    info = InfoOf(21, h, result);
    Check(info.progress == info.total, "draft complete");
    Check(info.show == GTOS_WINDOW_SHOW_DRAFTING, "complete draft still drafting");
    Check(bank.Present(21, Control(h)) == 0, "present frame 1");
    info = InfoOf(21, h, result);
    Check(info.frame_generation == 1, "generation 1");
    Check(info.show == GTOS_WINDOW_SHOW_PUBLISHED, "published show");
    Check(bank.Present(21, Control(h)) == GTOS_WINDOW_ERR_BAD_STATE, "repeat present refused");

    // Second frame on the same handle.
    FillFrame(21, h, 64, 8);
    Check(bank.Present(21, Control(h)) == 0, "present frame 2");
    info = InfoOf(21, h, result);
    Check(info.frame_generation == 2, "generation 2");

    // The provider reports the new frame on the same handle.
    gtos::gui::NativeWindowView view;
    gui::NativeImageSnapshot out;
    bool found = false;
    for (unsigned index = 0; index < gtos::gui::NativeWindowMaximum; ++index) {
        if (!bank.CopySlot(index, view, out)) continue;
        if (view.handle != h) continue;
        found = true;
        Check(view.lifecycle == gtos::gui::NativeWindowLive, "slot live");
        Check(view.frameGeneration == 2, "slot generation 2");
        Check(view.width == 4 && view.height == 4, "slot dimensions");
        Check(out.generation == 2 && out.width == 4 && out.height == 4, "snapshot metadata");
        Check(out.rgba[0] == 8 && out.rgba[3] == 255, "snapshot carries frame 2 pixels");
    }
    Check(found, "slot found for handle");
    bank.Close(21, Control(h));
}

void SubmitValidation() {
    int handle = bank.Create(31, CreateReq(4, 4));
    unsigned h = (unsigned)handle;
    GtosWindowSubmitRequest r;
    r.version = GTOS_WINDOW_ABI_VERSION;
    r.handle = h;
    r.offset = 0;
    r.source = 0;

    unsigned char bad[8] = {200, 10, 10, 100, 10, 10, 10, 255};
    r.length = 8;
    Check(bank.Submit(31, r, bad) == GTOS_WINDOW_ERR_BAD_PIXELS, "bad pixels rejected");
    int result = 0;
    Check(InfoOf(31, h, result).progress == 0, "bad pixels keep progress");
    Check(NativeWindowHostAccess::Draft(bank, h)[0] == 0, "bad pixels keep draft bytes");

    unsigned char good[4] = {10, 10, 10, 255};
    r.length = 0;
    Check(bank.Submit(31, r, good) == GTOS_WINDOW_ERR_BAD_SIZE, "zero length");
    r.length = 260;
    Check(bank.Submit(31, r, good) == GTOS_WINDOW_ERR_BAD_SIZE, "over limit");
    r.length = 6;
    Check(bank.Submit(31, r, good) == GTOS_WINDOW_ERR_BAD_SIZE, "not pixel aligned");
    r.length = 4;
    Check(bank.Submit(31, r, good) == 4, "aligned accepted");

    // A refused second chunk leaves the accepted draft exactly as it was.
    unsigned char draftSnapshot[8];
    WINMEMCPY(draftSnapshot, NativeWindowHostAccess::Draft(bank, h), sizeof(draftSnapshot));
    r.offset = 4;
    r.length = 4;
    unsigned char bad2[4] = {255, 0, 0, 1};
    Check(bank.Submit(31, r, bad2) == GTOS_WINDOW_ERR_BAD_PIXELS, "second bad chunk");
    Check(WINMEMCMP(draftSnapshot, NativeWindowHostAccess::Draft(bank, h),
                      sizeof(draftSnapshot)) == 0, "accepted draft preserved");
    Check(InfoOf(31, h, result).progress == 4, "progress preserved");
    bank.Close(31, Control(h));
}

void InputReadVersionGate() {
    // Every version-gate case goes through the SAME Call() an application uses.
    int handle = bank.Create(41, CreateReq(4, 4));
    unsigned h = (unsigned)handle;
    GtosInputReadRequest request;
    request.version = 0;
    request.handle = h;
    request.count = GTOS_INPUT_READ_LIMIT;
    request.destination = 2048;

    // Empty queue with a bad version must be refused, not return 0.
    unsigned address = PutRequest(&request, sizeof(request));
    WINMEMSET(arena.bytes + 2048, 0xA5, 64);
    Check(bank.Call(41, arena, GTOS_SYS_INPUT_READ, address, sizeof(request)) ==
          GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, "version 0 on empty queue");
    Check(arena.bytes[2048] == 0xA5, "destination sentinel untouched");

    // A queued normal record survives a bad-version read.
    bank.DeliverInput(41, h, Event(GTOS_INPUT_KEY_DOWN, 0x1e, 0, 0));
    Check(bank.Call(41, arena, GTOS_SYS_INPUT_READ, address, sizeof(request)) ==
          GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, "version 0 with record");
    Check(NativeWindowHostAccess::QueueCount(bank, h) == 1, "record not consumed");

    // Pending RESET also survives.
    bank.ResetInput(41, h);
    request.version = 2;
    PutRequest(&request, sizeof(request));
    Check(bank.Call(41, arena, GTOS_SYS_INPUT_READ, address, sizeof(request)) ==
          GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, "version 2 with reset pending");
    Check(NativeWindowHostAccess::ResetPending(bank, h), "reset still pending");

    // Closing CLOSE also survives.
    bank.RequestClose(41, h);
    Check(bank.Call(41, arena, GTOS_SYS_INPUT_READ, address, sizeof(request)) ==
          GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, "version 2 with close pending");
    Check(NativeWindowHostAccess::ClosePending(bank, h), "close still pending");

    // The correct version finally delivers.
    request.version = GTOS_WINDOW_ABI_VERSION;
    PutRequest(&request, sizeof(request));
    Check(bank.Call(41, arena, GTOS_SYS_INPUT_READ, address, sizeof(request)) == 1,
          "valid version reads close");
    Check(!NativeWindowHostAccess::ClosePending(bank, h), "close consumed");
    bank.Close(41, Control(h));
}

void DeliverInputReportsThisOverflow() {
    int handle = bank.Create(51, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    // Filling the ring must not report an overflow.
    for (unsigned i = 0; i < GTOS_INPUT_NORMAL_ENTRIES; ++i)
        Check(!bank.DeliverInput(51, h, Event(GTOS_INPUT_KEY_DOWN, i & 0xff, 0, 0)),
              "fill does not report overflow");
    Check(!NativeWindowHostAccess::ResetPending(bank, h), "no reset while filling");
    // The 127th delivery performs the overflow reset.
    Check(bank.DeliverInput(51, h, Event(GTOS_INPUT_KEY_DOWN, 0x42, 0, 0)),
          "overflow reported");
    Check(NativeWindowHostAccess::ResetPending(bank, h), "reset pending after overflow");
    // While the reset is still pending and the ring is not full again, a further
    // delivery must NOT report another overflow.
    Check(!bank.DeliverInput(51, h, Event(GTOS_INPUT_KEY_DOWN, 0x43, 0, 0)),
          "pending reset alone is not an overflow");
    // Refill to the next overflow: only then is it reported again.
    while (NativeWindowHostAccess::QueueCount(bank, h) < GTOS_INPUT_NORMAL_ENTRIES)
        Check(!bank.DeliverInput(51, h, Event(GTOS_INPUT_KEY_DOWN, 0x44, 0, 0)),
              "refill does not report overflow");
    Check(bank.DeliverInput(51, h, Event(GTOS_INPUT_KEY_DOWN, 0x45, 0, 0)),
          "second overflow reported");
    // The pending reset was merged, never duplicated.
    GtosInputReadRequest rr;
    rr.version = GTOS_WINDOW_ABI_VERSION;
    rr.handle = h;
    rr.count = GTOS_INPUT_READ_LIMIT;
    rr.destination = 4096;
    unsigned address = PutRequest(&rr, sizeof(rr));
    int delivered = bank.Call(51, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr));
    Check(delivered > 0, "read after overflow");
    const GtosInputEvent* events = (const GtosInputEvent*)(arena.bytes + 4096);
    unsigned resets = 0;
    for (int i = 0; i < delivered; ++i)
        if (events[i].type == GTOS_INPUT_RESET) ++resets;
    Check(resets == 1, "reset merged to one");
    Check(events[0].type == GTOS_INPUT_RESET, "reset precedes normal records");
    bank.Close(51, Control(h));
}

void DroppedSaturation() {
    int handle = bank.Create(52, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    for (unsigned i = 0; i < GTOS_INPUT_NORMAL_ENTRIES; ++i)
        bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 1, 0, 0));
    bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 2, 0, 0));
    Check(NativeWindowHostAccess::Dropped(bank, h) == GTOS_INPUT_NORMAL_ENTRIES,
          "first overflow drops exactly the ring");
    // Fill and overflow again: the count accumulates.
    while (NativeWindowHostAccess::QueueCount(bank, h) < GTOS_INPUT_NORMAL_ENTRIES)
        bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 3, 0, 0));
    bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 4, 0, 0));
    Check(NativeWindowHostAccess::Dropped(bank, h) == 2 * GTOS_INPUT_NORMAL_ENTRIES,
          "dropped accumulates");
    // Saturate at the maximum without wrapping.
    for (unsigned i = 0; i < 4; ++i) {
        while (NativeWindowHostAccess::QueueCount(bank, h) < GTOS_INPUT_NORMAL_ENTRIES)
            bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 5, 0, 0));
        bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 6, 0, 0));
    }
    Check(NativeWindowHostAccess::Dropped(bank, h) == 6 * GTOS_INPUT_NORMAL_ENTRIES,
          "dropped keeps accumulating");
    // Now exercise the real saturation path near the ceiling.
    NativeWindowHostAccess::SetDropped(bank, h, 0xffffffffU - 10);
    NativeWindowHostAccess::FillQueue(bank, h);
    Check(bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 7, 0, 0)),
          "overflow at the ceiling is reported");
    Check(NativeWindowHostAccess::Dropped(bank, h) == 0xffffffffU,
          "dropped saturates exactly at the ceiling");
    NativeWindowHostAccess::FillQueue(bank, h);
    Check(bank.DeliverInput(52, h, Event(GTOS_INPUT_KEY_DOWN, 8, 0, 0)),
          "overflow past the ceiling still reported");
    Check(NativeWindowHostAccess::Dropped(bank, h) == 0xffffffffU,
          "dropped stays saturated and never wraps");
    bank.Close(52, Control(h));
}

void InputReadTransaction() {
    int handle = bank.Create(61, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    GtosInputReadRequest rr;
    rr.version = GTOS_WINDOW_ABI_VERSION;
    rr.handle = h;
    rr.count = GTOS_INPUT_READ_LIMIT;
    rr.destination = 8192;
    unsigned address = PutRequest(&rr, sizeof(rr));

    // Empty queue returns 0 and copies nothing.
    WINMEMSET(arena.bytes + 8192, 0x5A, 64);
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 0,
          "empty queue returns zero");
    Check(arena.bytes[8192] == 0x5A, "empty queue copies nothing");

    // Negative coordinates survive the round trip unchanged.
    bank.DeliverInput(61, h, Event(GTOS_INPUT_POINTER_MOVE, 0, -7, -13));
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 1,
          "one record delivered");
    const GtosInputEvent* events = (const GtosInputEvent*)(arena.bytes + 8192);
    Check(events[0].type == GTOS_INPUT_POINTER_MOVE, "move type");
    Check(events[0].x == -7 && events[0].y == -13, "signed coordinates preserved");

    // A batch containing RESET is not consumed when the destination is bad.
    // ResetInput itself drops the unread normal ring, so queue the records after
    // the reset to build a batch of "one RESET plus two normal records".
    bank.ResetInput(61, h);
    bank.DeliverInput(61, h, Event(GTOS_INPUT_KEY_DOWN, 0x11, 0, 0));
    bank.DeliverInput(61, h, Event(GTOS_INPUT_KEY_UP, 0x11, 0, 0));
    Check(NativeWindowHostAccess::ResetPending(bank, h), "reset pending before batch");
    arena.writableLimit = 8;
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) ==
          GTOS_WINDOW_ERR_BAD_ADDRESS, "bad destination refused");
    Check(NativeWindowHostAccess::ResetPending(bank, h), "reset not consumed");
    Check(NativeWindowHostAccess::QueueCount(bank, h) == 2, "normal records not consumed");
    arena.writableLimit = sizeof(arena.bytes);

    // count bounds and a bad request pointer are separate refusals.
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr) - 1) ==
          GTOS_WINDOW_ERR_BAD_SIZE, "wire size gate");
    rr.count = 0;
    PutRequest(&rr, sizeof(rr));
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) ==
          GTOS_WINDOW_ERR_BAD_SIZE, "count zero refused");
    rr.count = 33;
    PutRequest(&rr, sizeof(rr));
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) ==
          GTOS_WINDOW_ERR_BAD_SIZE, "count over limit refused");
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, 0xF0000, sizeof(rr)) ==
          GTOS_WINDOW_ERR_BAD_ADDRESS, "bad request pointer refused");
    rr.count = GTOS_INPUT_READ_LIMIT;
    PutRequest(&rr, sizeof(rr));

    // The valid read consumes exactly the delivered records, RESET first.
    int delivered = bank.Call(61, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr));
    Check(delivered == 3, "reset plus two records");
    events = (const GtosInputEvent*)(arena.bytes + 8192);
    Check(events[0].type == GTOS_INPUT_RESET, "reset first in batch");
    Check(events[1].type == GTOS_INPUT_KEY_DOWN && events[2].type == GTOS_INPUT_KEY_UP,
          "normal records follow");
    Check(NativeWindowHostAccess::QueueCount(bank, h) == 0, "exactly n consumed");
    Check(!NativeWindowHostAccess::ResetPending(bank, h), "reset cleared");

    // Request and output genuinely overlap: the request sits at 16384 and the
    // output destination is the SAME address, so the response overwrites the
    // request bytes and the read must still use the pre-copy snapshot.
    unsigned char saved[sizeof(rr)];
    WINMEMCPY(saved, &rr, sizeof(rr));
    GtosInputReadRequest overlap;
    overlap.version = GTOS_WINDOW_ABI_VERSION;
    overlap.handle = h;
    overlap.count = GTOS_INPUT_READ_LIMIT;
    overlap.destination = 16384;
    WINMEMCPY(arena.bytes + 16384, &overlap, sizeof(overlap));
    bank.DeliverInput(61, h, Event(GTOS_INPUT_KEY_DOWN, 0x22, 0, 0));
    Check(bank.Call(61, arena, GTOS_SYS_INPUT_READ, 16384, sizeof(overlap)) == 1,
          "overlapping request/output still reads");
    events = (const GtosInputEvent*)(arena.bytes + 16384);
    Check(events[0].type == GTOS_INPUT_KEY_DOWN && events[0].code == 0x22,
          "overlapped output carries the queued record");
    WINMEMCPY(&rr, saved, sizeof(rr));
    bank.Close(61, Control(h));
}

void ResetDoesNotClearClose() {
    int handle = bank.Create(62, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    bank.DeliverInput(62, h, Event(GTOS_INPUT_KEY_DOWN, 1, 0, 0));
    bank.RequestClose(62, h);
    Check(NativeWindowHostAccess::ClosePending(bank, h), "close pending after request");
    // A focus-loss reset must not destroy the pending CLOSE of a closing object.
    bank.ResetInput(62, h);
    Check(NativeWindowHostAccess::ClosePending(bank, h), "reset keeps close pending");
    GtosInputReadRequest rr;
    rr.version = GTOS_WINDOW_ABI_VERSION;
    rr.handle = h;
    rr.count = GTOS_INPUT_READ_LIMIT;
    rr.destination = 24576;
    unsigned address = PutRequest(&rr, sizeof(rr));
    Check(bank.Call(62, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 1,
          "closing still yields one record");
    const GtosInputEvent* events = (const GtosInputEvent*)(arena.bytes + 24576);
    Check(events[0].type == GTOS_INPUT_CLOSE, "that record is CLOSE");
    bank.Close(62, Control(h));
}

void RequestCloseVersusDestroy() {
    int handle = bank.Create(71, CreateReq(4, 4));
    unsigned h = (unsigned)handle;
    FillFrame(71, h, 64, 3);
    Check(bank.Present(71, Control(h)) == 0, "frame before close request");
    const unsigned char* frontBefore = NativeWindowHostAccess::Front(bank, h);
    unsigned char frontSnapshot[16];
    WINMEMCPY(frontSnapshot, frontBefore, sizeof(frontSnapshot));

    bank.RequestClose(71, h);
    bank.RequestClose(71, h);
    GtosInputReadRequest rr;
    rr.version = GTOS_WINDOW_ABI_VERSION;
    rr.handle = h;
    rr.count = GTOS_INPUT_READ_LIMIT;
    rr.destination = 32768;
    unsigned address = PutRequest(&rr, sizeof(rr));
    Check(bank.Call(71, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 1,
          "single close event");
    Check(bank.Call(71, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 0,
          "close not re-queued");
    bank.RequestClose(71, h);
    Check(bank.Call(71, arena, GTOS_SYS_INPUT_READ, address, sizeof(rr)) == 0,
          "repeat request queues nothing");

    // closing rejects frame writes but keeps the published frame bytes.
    GtosWindowSubmitRequest sr;
    sr.version = GTOS_WINDOW_ABI_VERSION;
    sr.handle = h;
    sr.offset = 0;
    sr.source = 0;
    sr.length = 4;
    unsigned char pixel[4] = {1, 1, 1, 255};
    Check(bank.Submit(71, sr, pixel) == GTOS_WINDOW_ERR_BAD_STATE, "closing rejects submit");
    Check(bank.Present(71, Control(h)) == GTOS_WINDOW_ERR_BAD_STATE, "closing rejects present");
    Check(WINMEMCMP(frontSnapshot, NativeWindowHostAccess::Front(bank, h),
                      sizeof(frontSnapshot)) == 0, "closing keeps front bytes");

    int result = 0;
    GtosWindowInfoResponse info = InfoOf(71, h, result);
    Check(info.lifecycle == GTOS_WINDOW_CLOSING, "closing lifecycle visible");
    Check(info.frame_generation == 1, "closing keeps generation");

    // Application CLOSE destroys immediately.
    Check(bank.Close(71, Control(h)) == 0, "destroy");
    Check(bank.Close(71, Control(h)) == GTOS_WINDOW_ERR_BAD_HANDLE, "double destroy");

    // Owner reclaim destroys without queueing a CLOSE.
    int again = bank.Create(71, CreateReq(2, 2));
    Check(again > 0, "create before reclaim");
    bank.RequestClose(71, (unsigned)again);
    bank.ReclaimOwner(71);
    GtosWindowInfoRequest after;
    after.version = GTOS_WINDOW_ABI_VERSION;
    after.handle = (unsigned)again;
    after.result_va = 0;
    Check(bank.Info(71, after, info) == GTOS_WINDOW_ERR_BAD_HANDLE,
          "reclaim revoked the handle");
}

void ProviderSlots() {
    gtos::gui::NativeWindowView view;
    gui::NativeImageSnapshot out;
    // An illegal index is the only false, and it leaves both outputs untouched.
    WINMEMSET(&view, 0x7E, sizeof(view));
    WINMEMSET(&out, 0x7E, sizeof(out));
    gtos::gui::NativeWindowView viewSentinel = view;
    gui::NativeImageSnapshot outSentinel = out;
    Check(!bank.CopySlot(gtos::gui::NativeWindowMaximum, view, out), "out of range false");
    Check(WINMEMCMP(&view, &viewSentinel, sizeof(view)) == 0, "view untouched");
    Check(WINMEMCMP(&out, &outSentinel, sizeof(out)) == 0, "snapshot untouched");

    // Every in-range index is fully defined, including empty slots.
    unsigned dead = 0;
    for (unsigned index = 0; index < gtos::gui::NativeWindowMaximum; ++index) {
        WINMEMSET(&view, 0x7E, sizeof(view));
        WINMEMSET(&out, 0x7E, sizeof(out));
        Check(bank.CopySlot(index, view, out), "in-range always true");
        if (view.handle == 0) {
            ++dead;
            Check(view.lifecycle == gtos::gui::NativeWindowDead, "empty slot dead");
            Check(view.width == 0 && view.height == 0 && view.frameGeneration == 0,
                  "empty slot zeroed");
            Check(out.generation == 0 && out.width == 0 && out.height == 0,
                  "empty snapshot zeroed");
            Check(out.rgba[0] == 0, "empty snapshot pixels zeroed");
        }
    }
    Check(dead == gtos::gui::NativeWindowMaximum, "all slots empty at this point");

    // A created but unpublished object reports CREATE dimensions and zero frame.
    int handle = bank.Create(81, CreateReq(8, 2));
    Check(handle > 0, "create for provider");
    bool found = false;
    for (unsigned index = 0; index < gtos::gui::NativeWindowMaximum; ++index) {
        WINMEMSET(&out, 0x7E, sizeof(out));
        if (!bank.CopySlot(index, view, out)) continue;
        if (view.handle != (unsigned)handle) continue;
        found = true;
        Check(view.frameGeneration == 0, "unpublished generation zero");
        Check(view.width == 8 && view.height == 2, "CREATE dimensions reported");
        Check(out.generation == 0, "unpublished snapshot generation zero");
        Check(out.width == 8 && out.height == 2, "unpublished snapshot keeps CREATE size");
        Check(out.rgba[0] == 0, "unpublished snapshot pixels zeroed");
        Check(view.lifecycle == gtos::gui::NativeWindowLive, "unpublished live");
    }
    Check(found, "unpublished slot found");
    bank.Close(81, Control((unsigned)handle));
}

// The four trusted desktop operations resolve the owner from a non-reusable
// handle; a user system call can never reach them.
void ProviderDesktopOperations() {
    int handle = bank.Create(91, CreateReq(2, 2));
    unsigned h = (unsigned)handle;
    Check(bank.ContainsWindow(h), "contains live window");
    Check(!bank.ContainsWindow(0), "contains rejects zero handle");
    Check(!bank.ContainsWindow(h + 1000), "contains rejects stale handle");
    Check(bank.OwnerOfHandle(h) == 91, "owner resolved from handle");

    // RouteInput delivers, reports this-call overflow, and refuses closing.
    Check(bank.RouteInput(h, Event(GTOS_INPUT_KEY_DOWN, 0x33, 0, 0)) == 0,
          "route delivers");
    Check(NativeWindowHostAccess::QueueCount(bank, h) == 1, "route queued one record");
    for (unsigned i = 1; i < GTOS_INPUT_NORMAL_ENTRIES; ++i)
        Check(bank.RouteInput(h, Event(GTOS_INPUT_KEY_DOWN, 0x34, 0, 0)) == 0,
              "route fills without overflow");
    Check(bank.RouteInput(h, Event(GTOS_INPUT_KEY_DOWN, 0x35, 0, 0)) == 1,
          "route reports this-call overflow");

    // ResetWindowInput clears the normal ring and marks a reset.
    bank.ResetWindowInput(h);
    Check(NativeWindowHostAccess::ResetPending(bank, h), "desktop reset pending");
    Check(NativeWindowHostAccess::QueueCount(bank, h) == 0, "desktop reset cleared ring");

    // A stale handle cannot be routed to a reused slot.
    bank.RequestWindowClose(h);
    Check(bank.ContainsWindow(h), "closing still contains the window");
    Check(bank.RouteInput(h, Event(GTOS_INPUT_KEY_DOWN, 0x36, 0, 0)) ==
          GTOS_WINDOW_ERR_BAD_STATE, "route refuses closing");
    bank.ResetWindowInput(h);
    Check(NativeWindowHostAccess::ClosePending(bank, h),
          "desktop reset keeps a closing CLOSE");
    Check(bank.Close(91, Control(h)) == 0, "destroy");
    Check(!bank.ContainsWindow(h), "destroyed window not contained");
    Check(bank.RouteInput(h, Event(GTOS_INPUT_KEY_DOWN, 0x37, 0, 0)) ==
          GTOS_WINDOW_ERR_BAD_HANDLE, "route refuses stale handle");
    Check(bank.OwnerOfHandle(h) == 0, "no owner for stale handle");

    // A reused slot gets a new handle; the old one never reaches it.
    int fresh = bank.Create(91, CreateReq(2, 2));
    Check(fresh > handle, "reused slot has a new handle");
    Check(!bank.ContainsWindow(h), "old handle does not alias the new object");
    Check(bank.ContainsWindow((unsigned)fresh), "new handle is contained");
    bank.Close(91, Control((unsigned)fresh));
}

}  // namespace

int Run() {
    WINLOG("native window bank host test\n");
    WireLayout();
    FourOwnersFillSlots();
    HandleAndGenerationExhaustion();
    FramesAndPresent();
    SubmitValidation();
    InputReadVersionGate();
    DeliverInputReportsThisOverflow();
    DroppedSaturation();
    InputReadTransaction();
    ResetDoesNotClearClose();
    RequestCloseVersusDestroy();
    ProviderSlots();
    ProviderDesktopOperations();
    if (failures) {
        WINLOG("NATIVE WINDOW HOST FAIL %d\n", failures);
        return 1;
    }
    WINLOG("NATIVE WINDOW HOST PASS\n");
    return 0;
}

#ifdef GTOS_WINDOW_I386_HOST
// Real Linux ELF32 entry; same service and test cases as the sanitized x64 runs.
// A freestanding link has no CRT, so the static constructors are run explicitly.
extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);
extern "C" void _start() {
    for (void (**ctor)(void) = __init_array_start; ctor != __init_array_end; ++ctor)
        (*ctor)();
    const int result = Run();
    asm volatile("int $0x80" : : "a"(1), "b"(result) : "memory", "cc");
    for (;;) {}
}
#else
int main() { return Run(); }
#endif
