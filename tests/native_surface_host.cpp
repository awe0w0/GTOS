#include <process/native_surface.h>
#ifndef GTOS_SURFACE_I386_HOST
#include <cstdio>
#include <cstdlib>
#endif

using gtos::process::NativeSurfaceBank;
using gtos::gui::NativeImageSnapshot;
namespace gtos { namespace process {
    struct NativeSurfaceBankHostAccess {
        struct State {
            bool available, drafting;
            unsigned owner, handle, width, height, total, progress, nextHandle;
            unsigned char draft[4096];
            NativeImageSnapshot front;
        };
        static State Read(const NativeSurfaceBank& bank) {
            State out = {};
            out.available = bank.available; out.drafting = bank.drafting;
            out.owner = bank.owner; out.handle = bank.handle;
            out.width = bank.width; out.height = bank.height;
            out.total = bank.total; out.progress = bank.progress; out.nextHandle = bank.nextHandle;
            for (unsigned i = 0; i < sizeof(out.draft); ++i) out.draft[i] = bank.draft[i];
            out.front = bank.front;
            return out;
        }
        static void PositionHandle(NativeSurfaceBank& bank, unsigned value) { bank.nextHandle = value; }
    };
} }
#ifdef GTOS_SURFACE_I386_HOST
// Minimal memory shims for compiler-generated aggregate copies in the real
// freestanding Linux i386 host, without requiring an uninstalled multilib CRT.
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
#endif
namespace {
    typedef gtos::process::NativeSurfaceBankHostAccess Access;
    const unsigned MaximumUnsigned = ~0U;
    unsigned checks = 0, exhaustiveRequests = 0;
    void Text(const char* message) {
#ifdef GTOS_SURFACE_I386_HOST
        unsigned length = 0;
        while (message[length]) ++length;
        int result;
        asm volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(message), "d"(length) : "memory", "cc");
        (void)result;
#else
        std::fputs(message, stdout);
#endif
    }
    void Number(unsigned value) {
        char buffer[11]; unsigned end = sizeof(buffer) - 1; buffer[end] = 0;
        do { buffer[--end] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        Text(buffer + end);
    }
    void Check(bool condition, const char* label) {
        ++checks;
        if (condition) return;
        Text("FAIL "); Text(label); Text(" at check "); Number(checks); Text("\n");
#ifdef GTOS_SURFACE_I386_HOST
        asm volatile("int $0x80" : : "a"(1), "b"(1) : "memory", "cc");
        for (;;) {}
#else
        std::exit(1);
#endif
    }
    bool Equal(const void* left, const void* right, unsigned length) {
        const unsigned char* a = static_cast<const unsigned char*>(left);
        const unsigned char* b = static_cast<const unsigned char*>(right);
        for (unsigned i = 0; i < length; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    void Fill(void* bytes, unsigned length, unsigned char value) {
        unsigned char* out = static_cast<unsigned char*>(bytes);
        for (unsigned i = 0; i < length; ++i) out[i] = value;
    }
    void Unchanged(const NativeSurfaceBank& bank, const Access::State& before) {
        const Access::State after = Access::Read(bank);
        Check(after.available == before.available && after.drafting == before.drafting &&
              after.owner == before.owner && after.handle == before.handle &&
              after.width == before.width && after.height == before.height &&
              after.total == before.total && after.progress == before.progress &&
              after.nextHandle == before.nextHandle, "failure preserves metadata and handle sequence");
        Check(Equal(after.draft, before.draft, sizeof(after.draft)), "failure preserves every draft byte");
        Check(Equal(&after.front, &before.front, sizeof(after.front)), "failure preserves published snapshot");
    }
    void Scrubbed(const NativeSurfaceBank& bank) {
        const Access::State state = Access::Read(bank);
        Check(!state.drafting && !state.owner && !state.handle && !state.width &&
              !state.height && !state.total && !state.progress, "retired draft metadata scrubbed");
        for (unsigned i = 0; i < sizeof(state.draft); ++i) Check(state.draft[i] == 0, "retired draft byte scrubbed");
    }
    GtosSurfaceBeginRequest BeginRequest(unsigned width = 32, unsigned height = 32) {
        const GtosSurfaceBeginRequest out = {1, width, height, width * 4U, 1};
        return out;
    }
    GtosSurfaceWriteRequest WriteRequest(unsigned handle, unsigned offset = 0, unsigned length = 4) {
        const GtosSurfaceWriteRequest out = {1, handle, offset, 0x40001000U, length};
        return out;
    }
    GtosSurfaceControlRequest Control(unsigned handle) {
        const GtosSurfaceControlRequest out = {1, handle}; return out;
    }
    void BeginError(NativeSurfaceBank& bank, unsigned owner, const GtosSurfaceBeginRequest& request,
                    unsigned size, int expected) {
        const Access::State before = Access::Read(bank);
        Check(bank.Begin(owner, request, size) == expected, "BEGIN error classification"); Unchanged(bank, before);
    }
    void WriteError(NativeSurfaceBank& bank, unsigned owner, const GtosSurfaceWriteRequest& request,
                    unsigned size, const unsigned char* bytes, int expected) {
        const Access::State before = Access::Read(bank);
        Check(bank.Write(owner, request, size, bytes) == expected, "WRITE error classification"); Unchanged(bank, before);
    }
    void ControlError(NativeSurfaceBank& bank, unsigned owner, const GtosSurfaceControlRequest& request,
                      unsigned size, int expected) {
        const Access::State before = Access::Read(bank);
        Check(bank.Present(owner, request, size) == expected, "PRESENT error classification"); Unchanged(bank, before);
        Check(bank.Abort(owner, request, size) == expected, "ABORT error classification"); Unchanged(bank, before);
    }
    unsigned Start(NativeSurfaceBank& bank, unsigned owner = 7, unsigned width = 32, unsigned height = 32) {
        const int result = bank.Begin(owner, BeginRequest(width, height), 20);
        Check(result > 0, "BEGIN returns positive signed handle"); return static_cast<unsigned>(result);
    }
    void Pixels(unsigned char* bytes, unsigned width, unsigned height) {
        // Independent coordinate oracle includes transparent, translucent and opaque pixels.
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            const unsigned i = (y * width + x) * 4;
            const unsigned char alpha = static_cast<unsigned char>(x * 17 + y * 29);
            bytes[i] = alpha / 2; bytes[i + 1] = static_cast<unsigned char>(alpha & (x * 13));
            bytes[i + 2] = static_cast<unsigned char>(alpha & (y * 11)); bytes[i + 3] = alpha;
        }
    }
    void Submit(NativeSurfaceBank& bank, unsigned owner, unsigned handle,
                const unsigned char* bytes, unsigned total, unsigned offset = 0) {
        while (offset < total) {
            unsigned length = 4 * (1 + ((offset / 4) % 64));
            if (length > total - offset) length = total - offset;
            const GtosSurfaceWriteRequest request = WriteRequest(handle, offset, length);
            const Access::State before = Access::Read(bank);
            Check(bank.ValidateWrite(owner, request, 20) == 0, "valid chunk preflight"); Unchanged(bank, before);
            Check(bank.Write(owner, request, 20, bytes + offset) == static_cast<int>(length), "valid chunk copied");
            offset += length;
        }
    }
    NativeImageSnapshot Snapshot(NativeSurfaceBank& bank, unsigned handle, unsigned width,
                                 unsigned height, const unsigned char* bytes) {
        NativeImageSnapshot out; Fill(&out, sizeof(out), 0xA5);
        Check(bank.CopyLatest(0, out), "published snapshot visible");
        Check(out.generation == handle && out.width == width && out.height == height, "exact snapshot metadata");
        Check(Equal(out.rgba, bytes, width * height * 4), "all pixels match independent oracle");
        for (unsigned i = width * height * 4; i < sizeof(out.rgba); ++i) Check(out.rgba[i] == 0, "published tail cleared");
        NativeImageSnapshot untouched; Fill(&untouched, sizeof(untouched), 0x5A);
        const NativeImageSnapshot sentinel = untouched;
        Check(!bank.CopyLatest(handle, untouched), "known generation has no update");
        Check(Equal(&untouched, &sentinel, sizeof(untouched)), "unchanged CopyLatest never touches output");
        return out;
    }
    void InitialAndBeginBoundaries() {
        NativeSurfaceBank bank; NativeImageSnapshot out; Fill(&out, sizeof(out), 0xDA);
        const NativeImageSnapshot sentinel = out;
        Check(!bank.CopyLatest(0, out), "no initial publication");
        Check(Equal(&out, &sentinel, sizeof(out)), "initial CopyLatest output unchanged"); Scrubbed(bank);
        BeginError(bank, 7, BeginRequest(), 20, GTOS_SURFACE_ERR_UNAVAILABLE);
        bank.SetAvailable(true); BeginError(bank, 0, BeginRequest(), 20, GTOS_SURFACE_ERR_BAD_STATE);
        const unsigned sizes[] = {0, 1, 19, 21, 256, MaximumUnsigned};
        for (unsigned size : sizes) BeginError(bank, 7, BeginRequest(), size, GTOS_SURFACE_ERR_BAD_SIZE);
        const unsigned values[] = {0, 2, 0x80000000U, MaximumUnsigned};
        for (unsigned value : values) {
            GtosSurfaceBeginRequest request = BeginRequest(); request.version = value;
            BeginError(bank, 7, request, 20, GTOS_SURFACE_ERR_UNSUPPORTED_VERSION);
            request = BeginRequest(); request.format = value;
            BeginError(bank, 7, request, 20, GTOS_SURFACE_ERR_UNSUPPORTED_FORMAT);
        }
        const unsigned dimensions[] = {0, 1, 31, 32, 33, 0x40000000U, 0x7fffffffU, 0x80000000U, MaximumUnsigned};
        for (unsigned width : dimensions) for (unsigned height : dimensions) {
            if (width && height && width <= 32 && height <= 32) continue;
            const int expected = (!width || !height) ? GTOS_SURFACE_ERR_RANGE : GTOS_SURFACE_ERR_TOO_LARGE;
            BeginError(bank, 7, BeginRequest(width, height), 20, expected);
        }
        const unsigned widths[] = {1, 32};
        const unsigned strides[] = {0, 1, 3, 4, 5, 127, 128, 129, 0x80000000U, MaximumUnsigned};
        for (unsigned width : widths) for (unsigned stride : strides) {
            if (stride == width * 4) continue;
            GtosSurfaceBeginRequest request = BeginRequest(width, 32); request.stride = stride;
            BeginError(bank, 7, request, 20, GTOS_SURFACE_ERR_BAD_SIZE);
        }
        const unsigned handle = Start(bank); Check(handle == 1, "failed BEGIN consumes no handle");
        BeginError(bank, 7, BeginRequest(), 20, GTOS_SURFACE_ERR_BUSY);
        BeginError(bank, 8, BeginRequest(), 20, GTOS_SURFACE_ERR_BUSY);
        Check(bank.Abort(7, Control(handle), 8) == 0, "abort initial draft"); Scrubbed(bank);
    }
    void WriteAndControlBoundaries() {
        NativeSurfaceBank bank; bank.SetAvailable(true); const unsigned handle = Start(bank);
        unsigned char bytes[4096]; Pixels(bytes, 32, 32);
        const unsigned sizes[] = {0, 1, 19, 21, 256, MaximumUnsigned};
        for (unsigned size : sizes) WriteError(bank, 7, WriteRequest(handle), size, bytes, GTOS_SURFACE_ERR_BAD_SIZE);
        const unsigned values[] = {0, 2, 0x80000000U, MaximumUnsigned};
        for (unsigned value : values) {
            GtosSurfaceWriteRequest request = WriteRequest(handle); request.version = value;
            WriteError(bank, 7, request, 20, bytes, GTOS_SURFACE_ERR_UNSUPPORTED_VERSION);
            GtosSurfaceControlRequest control = Control(handle); control.version = value;
            ControlError(bank, 7, control, 8, GTOS_SURFACE_ERR_UNSUPPORTED_VERSION);
        }
        const unsigned owners[] = {0, 8, MaximumUnsigned};
        for (unsigned owner : owners) {
            WriteError(bank, owner, WriteRequest(handle), 20, bytes, GTOS_SURFACE_ERR_BAD_STATE);
            ControlError(bank, owner, Control(handle), 8, GTOS_SURFACE_ERR_BAD_STATE);
        }
        const unsigned handles[] = {0, handle + 1, 0x80000000U, MaximumUnsigned};
        for (unsigned badHandle : handles) {
            WriteError(bank, 7, WriteRequest(badHandle), 20, bytes, GTOS_SURFACE_ERR_BAD_STATE);
            ControlError(bank, 7, Control(badHandle), 8, GTOS_SURFACE_ERR_BAD_STATE);
        }
        const unsigned controlSizes[] = {0, 1, 7, 9, 20, MaximumUnsigned};
        for (unsigned size : controlSizes) ControlError(bank, 7, Control(handle), size, GTOS_SURFACE_ERR_BAD_SIZE);
        for (unsigned length = 0; length <= 257; ++length) {
            const Access::State before = Access::Read(bank);
            const int expected = length > 256 ? GTOS_SURFACE_ERR_TOO_LARGE :
                (!length || length % 4 ? GTOS_SURFACE_ERR_BAD_SIZE : 0);
            Check(bank.ValidateWrite(7, WriteRequest(handle, 0, length), 20) == expected, "length preflight matrix");
            Unchanged(bank, before);
            if (expected) WriteError(bank, 7, WriteRequest(handle, 0, length), 20, bytes, expected);
        }
        const unsigned huge[] = {0x80000000U, MaximumUnsigned};
        for (unsigned length : huge) WriteError(bank, 7, WriteRequest(handle, 0, length), 20, bytes, GTOS_SURFACE_ERR_TOO_LARGE);
        WriteError(bank, 7, WriteRequest(handle), 20, 0, GTOS_SURFACE_ERR_BAD_ADDRESS);
        const Access::State empty = Access::Read(bank);
        Check(bank.Present(7, Control(handle), 8) == GTOS_SURFACE_ERR_BAD_STATE, "empty PRESENT rejected"); Unchanged(bank, empty);
        unsigned char invalid[256]; const unsigned positions[] = {0, 31, 63};
        for (unsigned component = 0; component < 3; ++component) for (unsigned pixel : positions) {
            Fill(invalid, sizeof(invalid), 0x7F); invalid[pixel * 4 + component] = 0x80;
            WriteError(bank, 7, WriteRequest(handle, 0, 256), 20, invalid, GTOS_SURFACE_ERR_BAD_PIXELS);
        }
        Check(bank.Write(7, WriteRequest(handle), 20, bytes) == 4, "first valid pixel progress");
        const Access::State partial = Access::Read(bank);
        Check(partial.progress == 4 && Equal(partial.draft, bytes, 4), "exact valid write progress");
        Check(bank.Present(7, Control(handle), 8) == GTOS_SURFACE_ERR_BAD_STATE, "partial PRESENT rejected"); Unchanged(bank, partial);
        Fill(invalid, sizeof(invalid), 0x7F); invalid[255] = 0;
        WriteError(bank, 7, WriteRequest(handle, 4, 256), 20, invalid, GTOS_SURFACE_ERR_BAD_PIXELS);
        Submit(bank, 7, handle, bytes, 4092, 4);
        const Access::State beforeMatrix = Access::Read(bank);
        // Widened endpoint oracle differs from the service's subtraction check.
        for (unsigned offset = 0; offset <= 4100; ++offset) for (unsigned length = 0; length <= 257; ++length) {
            const unsigned long long end = static_cast<unsigned long long>(offset) + length;
            const int expected = length > 256 ? GTOS_SURFACE_ERR_TOO_LARGE :
                (!length || length % 4 ? GTOS_SURFACE_ERR_BAD_SIZE :
                 (offset != 4092 || end > 4096 ? GTOS_SURFACE_ERR_RANGE : 0));
            Check(bank.ValidateWrite(7, WriteRequest(handle, offset, length), 20) == expected, "exhaustive widened endpoint oracle");
            ++exhaustiveRequests;
        }
        Unchanged(bank, beforeMatrix);
        const unsigned offsets[] = {4096, 4097, 0x7fffffffU, 0x80000000U, MaximumUnsigned};
        for (unsigned offset : offsets) WriteError(bank, 7, WriteRequest(handle, offset), 20, bytes, GTOS_SURFACE_ERR_RANGE);
        Check(bank.Write(7, WriteRequest(handle, 4092), 20, bytes + 4092) == 4, "last pixel completes draft");
        WriteError(bank, 7, WriteRequest(handle, 4096), 20, bytes, GTOS_SURFACE_ERR_RANGE);
        Check(bank.Present(7, Control(handle), 8) == 0, "complete PRESENT succeeds"); Scrubbed(bank);
        Snapshot(bank, handle, 32, 32, bytes);
        WriteError(bank, 7, WriteRequest(handle), 20, bytes, GTOS_SURFACE_ERR_BAD_STATE);
        ControlError(bank, 7, Control(handle), 8, GTOS_SURFACE_ERR_BAD_STATE);
    }
    void AllDimensions() {
        NativeSurfaceBank bank; bank.SetAvailable(true); unsigned char bytes[4096]; unsigned lastHandle = 0;
        for (unsigned width = 1; width <= 32; ++width) for (unsigned height = 1; height <= 32; ++height) {
            const unsigned handle = Start(bank, 7, width, height); Check(handle > lastHandle, "generation monotonically increases");
            Pixels(bytes, width, height); Submit(bank, 7, handle, bytes, width * height * 4);
            Check(bank.Present(7, Control(handle), 8) == 0, "all legal dimensions present"); Scrubbed(bank);
            NativeImageSnapshot out = Snapshot(bank, handle, width, height, bytes); Fill(&out, sizeof(out), 0xEF);
            Check(bank.CopyLatest(lastHandle, out), "old generation obtains new snapshot");
            Check(Equal(out.rgba, bytes, width * height * 4), "caller mutation cannot change publication"); lastHandle = handle;
        }
        const unsigned handle = Start(bank, 7, 1, 1); Pixels(bytes, 1, 1); Submit(bank, 7, handle, bytes, 4);
        Check(bank.Present(7, Control(handle), 8) == 0, "largest-to-smallest replacement"); Snapshot(bank, handle, 1, 1, bytes);
    }
    void OwnerLifecycle() {
        NativeSurfaceBank bank; bank.SetAvailable(true); unsigned char bytes[4096]; Pixels(bytes, 32, 32);
        const unsigned published = Start(bank); Submit(bank, 7, published, bytes, 4096);
        Check(bank.Present(7, Control(published), 8) == 0, "lifecycle publication");
        const NativeImageSnapshot saved = Snapshot(bank, published, 32, 32, bytes);
        bank.ReclaimOwner(7); Access::State state = Access::Read(bank);
        Check(Equal(&state.front, &saved, sizeof(saved)), "publication survives producer exit");
        const unsigned draft = Start(bank); Check(draft == published + 1, "new draft gets new handle");
        Check(bank.Write(7, WriteRequest(draft, 0, 256), 20, bytes) == 256, "unfinished nonzero draft");
        Check(Access::Read(bank).draft[7] != 0, "owner reclaim must scrub real nonzero pixels");
        const Access::State pending = Access::Read(bank);
        bank.ReclaimOwner(0); Unchanged(bank, pending); bank.ReclaimOwner(8); Unchanged(bank, pending);
        bank.ReclaimOwner(7); Scrubbed(bank); state = Access::Read(bank);
        Check(Equal(&state.front, &saved, sizeof(saved)), "owner reclaim preserves fixed snapshot");
        WriteError(bank, 7, WriteRequest(draft), 20, bytes, GTOS_SURFACE_ERR_BAD_STATE);
        ControlError(bank, 7, Control(draft), 8, GTOS_SURFACE_ERR_BAD_STATE);
        const unsigned other = Start(bank, 8); Check(other == draft + 1, "reclaimed handle never reused");
        WriteError(bank, 7, WriteRequest(other), 20, bytes, GTOS_SURFACE_ERR_BAD_STATE);
        ControlError(bank, 7, Control(other), 8, GTOS_SURFACE_ERR_BAD_STATE);
        Check(bank.Write(8, WriteRequest(other, 0, 256), 20, bytes) == 256, "new owner writes nonzero draft");
        Check(bank.Abort(8, Control(other), 8) == 0, "ABORT own draft"); Scrubbed(bank); state = Access::Read(bank);
        Check(Equal(&state.front, &saved, sizeof(saved)), "ABORT preserves publication");
        const unsigned next = Start(bank, MaximumUnsigned, 1, 1); Check(next == other + 1, "all nonzero owner IDs supported");
        bank.ReclaimOwner(MaximumUnsigned); Scrubbed(bank);
    }
    void HandleExhaustion() {
        NativeSurfaceBank bank; bank.SetAvailable(true);
        // Position only the private counter, then execute unchanged real code.
        Access::PositionHandle(bank, 0x7ffffffeU);
        const unsigned aborted = Start(bank, 7, 1, 1); Check(aborted == 0x7ffffffeU, "penultimate positive signed handle");
        Check(bank.Abort(7, Control(aborted), 8) == 0, "abort penultimate handle");
        const unsigned handle = Start(bank, 8, 1, 1); Check(handle == 0x7fffffffU, "last handle is INT_MAX");
        const unsigned char pixel[] = {127, 64, 32, 255};
        Check(bank.Write(8, WriteRequest(handle), 20, pixel) == 4, "last handle accepts pixels");
        Check(bank.Present(8, Control(handle), 8) == 0, "last handle can publish"); Snapshot(bank, handle, 1, 1, pixel);
        BeginError(bank, 7, BeginRequest(1, 1), 20, GTOS_SURFACE_ERR_RANGE);
        BeginError(bank, 8, BeginRequest(1, 1), 20, GTOS_SURFACE_ERR_RANGE);
        ControlError(bank, 8, Control(handle), 8, GTOS_SURFACE_ERR_BAD_STATE);
        Check(Access::Read(bank).nextHandle == 0x80000000U, "counter never wraps or reuses");
    }
    int Run() {
        InitialAndBeginBoundaries(); WriteAndControlBoundaries(); AllDimensions(); OwnerLifecycle(); HandleExhaustion();
        Text("NATIVE SURFACE HOST PASS checks="); Number(checks); Text(" exhaustive_write_requests="); Number(exhaustiveRequests);
        Text(" dimensions=1024 pointer_bits="); Number(sizeof(void*) * 8); Text("\n"); return 0;
    }
}
#ifdef GTOS_SURFACE_I386_HOST
// Real Linux ELF32 entry; same service and test cases as sanitized x64 runs.
extern "C" void _start() {
    const int result = Run();
    asm volatile("int $0x80" : : "a"(1), "b"(result) : "memory", "cc");
    for (;;) {}
}
#else
int main() { return Run(); }
#endif
