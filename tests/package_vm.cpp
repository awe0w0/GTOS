// Freestanding Linux integration test of the REAL guest validator and VM.
// Run from the repository root via tests/package_vm.sh. No 32-bit libc needed.
#include <apps/package.h>
#include <apps/vm.h>

using namespace gtos::apps;
asm(".section .rodata\n"
    ".global catch_package_start\n"
    "catch_package_start:\n"
    ".incbin \"apps/catch.gtapp\"\n"
    ".global catch_package_end\n"
    "catch_package_end:\n"
    ".previous\n");
extern "C" const uint8_t catch_package_start[], catch_package_end[];

static void Print(const char* message) {
    uint32_t size = 0;
    while (message[size]) ++size;
#ifdef __x86_64__
    uint64_t result;
    asm volatile("syscall" : "=a"(result) : "a"(1), "D"(2), "S"(message), "d"(size)
                 : "rcx", "r11", "memory");
#else
    asm volatile("int $0x80" : : "a"(4), "b"(2), "c"(message), "d"(size) : "memory");
#endif
}
static void Exit(uint32_t status) {
#ifdef __x86_64__
    asm volatile("syscall" : : "a"(60), "D"(status) : "rcx", "r11", "memory");
#else
    asm volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
#endif
    __builtin_unreachable();
}
static void Check(bool ok, const char* reason) {
    if (!ok) { Print("FAIL: "); Print(reason); Print("\n"); Exit(1); }
}

class CaptureHost : public Host {
public:
    int32_t paddleX, ballX, ballY, score;
    uint32_t frames, titles, summaries;
    bool outOfBounds;
    CaptureHost() : paddleX(-1), ballX(-1), ballY(-1), score(-1),
                    frames(0), titles(0), summaries(0), outOfBounds(false) {}
    virtual void Clear(uint8_t color) { ++frames; Check(color == 1, "Catch background"); }
    virtual void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color) {
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 272 || y + h > 128)
            outOfBounds = true;
        if (w == 32 && h == 5 && color == 8) paddleX = x;
        if (w == 6 && h == 6 && color == 12) { ballX = x; ballY = y; }
    }
    virtual void Text(int32_t x, int32_t y, const char* text, uint8_t color) {
        Check(x >= 0 && y >= 0 && y <= 120 && color == 15, "text clipping/palette");
        if (text[0] == 'C') ++titles;
        if (text[0] == 'A') ++summaries;
    }
    virtual void Number(int32_t x, int32_t y, int32_t value, uint8_t color) {
        Check(x == 232 && y == 7 && color == 15, "score coordinates");
        score = value;
    }
};

static uint8_t modified[PackageLimit];
static void ClonePackage(uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) modified[i] = catch_package_start[i];
}

static void Test() {
    const uint32_t size = catch_package_end - catch_package_start;
    PackageInfo info;
    Check(ValidatePackage(catch_package_start, size, &info) == PackageOK, "Python/C++ package ABI");
    Check(info.instructionCount > 50 && info.size == size, "external executable package");
    Check(CRC32((const uint8_t*)"123456789", 9) == 0xCBF43926U, "standard CRC32 vector");
    Check(PackageCRC(catch_package_start, size) == Read32(catch_package_start + 120), "package CRC agreement");

    CaptureHost host;
    VirtualMachine vm(&host);
    Check(vm.Load(catch_package_start, size), "load Catch");
    Check(vm.Step(0, 0), "initialize Catch");
    Check(host.paddleX == 120 && host.ballY == 24 && host.score == 0, "initial state");
    Check(host.frames == 1 && host.titles == 1 && host.summaries == 1, "initial draw services");
    Check(vm.Step(VirtualMachine::Left, 4), "tick gate yields");
    Check(host.paddleX == 120 && host.ballY == 24 && host.frames == 1, "tick gate blocks early update");
    Check(vm.Step(VirtualMachine::Left, 5), "left key");
    Check(host.paddleX == 110 && host.ballY == 27, "real VM movement and fall");
    Check(vm.Step(VirtualMachine::Right, 10) && host.paddleX == 120, "right key");
    Check(vm.Step(VirtualMachine::Left | VirtualMachine::Right, 15) && host.paddleX == 120, "both keys cancel");

    uint32_t ticks = 15;
    for (uint32_t i = 0; i < 30; ++i) {
        ticks += 5;
        Check(vm.Step(VirtualMachine::Left, ticks), "left edge sustained input");
    }
    Check(host.paddleX == 0, "left edge clamps");
    for (uint32_t i = 0; i < 30; ++i) {
        ticks += 5;
        Check(vm.Step(VirtualMachine::Right, ticks), "right edge sustained input");
    }
    Check(host.paddleX == 240, "right edge clamps");
    Check(vm.Step(VirtualMachine::Action, ticks += 5), "restart after movement");
    Check(host.paddleX == 120 && host.ballY == 24 && host.score == 0, "restart initial state");
    Check(vm.Step(VirtualMachine::Action | VirtualMachine::Right, ticks += 5), "held action");
    Check(host.paddleX == 130 && host.ballY == 27, "restart is edge triggered");

    // Steer using only rendered geometry, just as a player would. The test
    // cannot access the VM's private registers or implement collisions.
    int32_t bestScore = 0;
    for (uint32_t i = 0; i < 1200; ++i) {
        uint32_t key = 0;
        int32_t delta = host.ballX + 3 - (host.paddleX + 16);
        if (delta < -5) key = VirtualMachine::Left;
        if (delta > 5) key = VirtualMachine::Right;
        Check(vm.Step(key, ticks += 5), "auto-play continues yielding");
        if (host.score > bestScore) bestScore = host.score;
    }
    Check(bestScore >= 2, "actual bytecode catches and scores repeatedly");
    Check(!host.outOfBounds && !vm.Fault(), "draw bounds and healthy execution");

    // Intentionally dodge until one falling object is missed.
    bool observedMiss = false;
    for (uint32_t i = 0; i < 60; ++i) {
        uint32_t key = host.ballX < 136 ? VirtualMachine::Right : VirtualMachine::Left;
        Check(vm.Step(key, ticks += 5), "dodge continues yielding");
        if (host.score == 0 && host.ballY == 24) { observedMiss = true; break; }
    }
    Check(observedMiss, "miss resets score and respawns");
    Check(vm.Step(VirtualMachine::Action, ticks += 5), "restart after gameplay");
    Check(host.paddleX == 120 && host.score == 0 && host.ballY == 24, "restart resets gameplay");
    vm.Stop();
    Check(!vm.Running() && !vm.Step(0, ticks), "stop is terminal until reset");
    vm.Reset();
    Check(vm.Step(0, ticks) && host.paddleX == 120 && host.score == 0, "reset relaunches package");

    // Runtime uses a private copy: mutation after Load cannot rewrite code.
    ClonePackage(size);
    Check(vm.Load(modified, size), "load mutable input");
    modified[128] = 255;
    Check(vm.Step(0, ticks), "loaded code is privately copied");
    Check(!vm.Load(modified, size) && !vm.Running(), "corrupt package stops old instance");
    Check(vm.Fault() != 0, "corrupt package reports a fault");

    // Syntactically valid infinite loop must fault within one bounded Step.
    ClonePackage(size);
    modified[128] = 10;
    modified[129] = modified[130] = modified[131] = 0;
    Write32(modified + 132, 0);
    Write32(modified + 120, PackageCRC(modified, size));
    Check(ValidatePackage(modified, size) == PackageOK, "loop remains a valid package");
    Check(vm.Load(modified, size), "load budget fixture");
    Check(!vm.Step(0, 0) && !vm.Running() && vm.Fault(), "instruction budget stops infinite loop");

    // A CRC-valid illegal register is refused before any instruction executes.
    ClonePackage(size);
    modified[129] = 32;
    Write32(modified + 120, PackageCRC(modified, size));
    Check(ValidatePackage(modified, size) == PackageInstruction, "invalid register rejection");
    Check(!vm.Load(modified, size), "invalid instruction cannot run");
    VirtualMachine noHost(0);
    Check(!noHost.Load(catch_package_start, size), "missing host is reported");
}

#ifdef __x86_64__
// Linux enters _start without a C return address. Align before a normal call.
asm(".text\n.global _start\n_start:\n"
    "xor %rbp,%rbp\nand $-16,%rsp\ncall TestEntry\n");
extern "C" void TestEntry() {
#else
extern "C" void _start() {
#endif
    Test();
    Print("PASS: actual C++ package validator and Catch VM integration\n");
    Exit(0);
}
