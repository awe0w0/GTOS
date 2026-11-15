#include <apps/vm.h>
#include <common/boot_log.h>
#include <hardwarecommunication/cpu_hal.h>
#include <memorymanagement.h>

extern const uint8_t catch_start[] asm("_binary_catch_gtapp_start");
extern const uint8_t catch_end[] asm("_binary_catch_gtapp_end");

namespace {
    using namespace gtos::hardwarecommunication::cpu;
    using gtos::common::BootLog;
    using gtos::apps::VirtualMachine;

    bool CheckInterrupts() {
        const InterruptState outer = MaskInterrupts();
        const InterruptState inner = MaskInterrupts();
        RestoreInterrupts(inner);
        const InterruptState held = MaskInterrupts();
        RestoreInterrupts(outer);
        const InterruptState resumed = MaskInterrupts();
        RestoreInterrupts(resumed);
        return outer == InterruptsUnmasked && inner == InterruptsMasked
            && held == InterruptsMasked && resumed == InterruptsUnmasked;
    }

    bool CheckLog() {
        const char message[] = "GTOS on RISC-V";
        BootLog::Reset();
        for (const char* character = message; *character; ++character)
            BootLog::Put(*character);
        char line[sizeof(message)];
        if (!BootLog::ReadLine(0, line, sizeof(line)) || BootLog::Lines() != 1)
            return false;
        for (unsigned i = 0; i < sizeof(message); ++i)
            if (line[i] != message[i]) return false;
        return true;
    }

    class CatchFrame : public gtos::apps::Host {
    public:
        int32_t paddle = -1;
        unsigned frames = 0;

        void Clear(uint8_t) override { ++frames; }
        void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t) override {
            if (y == 105 && w == 32 && h == 5) paddle = x;
        }
        void Text(int32_t, int32_t, const char*, uint8_t) override {}
        void Number(int32_t, int32_t, int32_t, uint8_t) override {}
    };

    bool CheckPackage(gtos::MemoryManager& memory) {
        static CatchFrame frame;
        static VirtualMachine vm(&frame);
        const unsigned bytes = reinterpret_cast<unsigned>(catch_end)
            - reinterpret_cast<unsigned>(catch_start);
        if (bytes < gtos::apps::HeaderSize || bytes > gtos::apps::PackageLimit) return false;
        uint8_t* copy = static_cast<uint8_t*>(memory.malloc(bytes));
        if (!copy) return false;
        for (unsigned i = 0; i < bytes; ++i) copy[i] = catch_start[i];
        // Exercise both the unchanged .gtapp validation and the game input path.
        copy[0] ^= 1;
        bool rejected = !vm.Load(copy, bytes);
        copy[0] ^= 1;
        frame.frames = 0;
        frame.paddle = -1;
        bool moved = vm.Load(copy, bytes) && vm.Step(0, 0) && frame.paddle == 120
            && vm.Step(VirtualMachine::Right, 5) && frame.paddle == 130 && frame.frames == 2;
        return memory.tryFree(copy) && rejected && moved;
    }
}

extern "C" unsigned gtos_riscv_boot(void* arena, unsigned bytes) {
    gtos::MemoryManager memory(reinterpret_cast<unsigned>(arena), bytes, false);
    unsigned failed = CheckInterrupts() ? 0U : 1U;
    if (!CheckLog()) failed |= 2U;
    if (!CheckPackage(memory)) failed |= 4U;
    const gtos::HeapStatistics heap = memory.getStatistics();
    if (!heap.valid || heap.totalBytes != bytes || heap.allocatedBlocks != 0
        || heap.freeBlocks != 1 || heap.invalidFrees != 0) failed |= 8U;
    return failed;
}
