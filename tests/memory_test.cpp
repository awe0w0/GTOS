#include <memorymanagement.h>
#include <memory/physical.h>
#include <memory/selftest.h>
using namespace gtos;
using namespace gtos::memory;

static int failures = 0;
static uint32_t checks = 0;
static void print(const char* message) {
    uint32_t length = 0;
    while (message[length]) ++length;
    { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4), "b"(1), "c"(message), "d"(length)  : "memory", "cc"); }
}
static void number(uint32_t value) {
    char digits[11];
    uint32_t cursor = 10;
    digits[cursor] = 0;
    do { digits[--cursor] = '0' + value % 10; value /= 10; } while (value);
    print(digits + cursor);
}
static void check(bool condition, uint32_t line) {
    ++checks;
    if (!condition) { ++failures; print("FAIL line "); number(line); print("\n"); }
}
#define CHECK(condition) check((condition), __LINE__)

static uint8_t arena[32768] __attribute__((aligned(4096)));
static uint8_t mapBuffer[256] __attribute__((aligned(16)));
static MultibootInfo info;
static MultibootModule modules[2];
static char moduleName[] = "test-module";
static char command[] = "test=memory";
static uint8_t section[40];
static uint8_t physicalStorage[sizeof(PhysicalMemoryManager)] __attribute__((aligned(4096)));

static void zero(void* destination, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) ((uint8_t*)destination)[i] = 0;
}
static uint32_t appendMap(uint32_t offset, uint64_t address, uint64_t length, uint32_t type, uint32_t extra = 0) {
    MultibootMemoryMapEntry* entry = (MultibootMemoryMapEntry*)(mapBuffer + offset);
    entry->size = 20 + extra;
    entry->address = address;
    entry->length = length;
    entry->type = type;
    return offset + sizeof(MultibootMemoryMapEntry) + extra;
}
static void mapInfo(uint32_t length) {
    zero(&info, sizeof(info));
    info.flags = 1u << 6;
    info.memoryMap = (uint32_t)mapBuffer;
    info.memoryMapLength = length;
}
static void heapTests() {
    CHECK(RunHeapSelfTest());
    CHECK(MemoryManager::activeMemoryManager == 0);
    CHECK(::operator new(8) == 0);
    MemoryManager heap((size_t)arena + 1, sizeof(arena) - 1);
    CHECK(heap.validate());
    const HeapStatistics initial = heap.getStatistics();
    CHECK(initial.totalBytes == sizeof(arena) - 16);
    CHECK(initial.freeBytes == initial.totalBytes - sizeof(MemoryChunk));
    void* p = heap.malloc(1);
    CHECK(p && !((size_t)p & 15));
    CHECK(heap.getStatistics().usedBytes == 16);
    CHECK(!heap.tryFree((uint8_t*)p + 1));
    CHECK(!heap.tryFree(arena));
    CHECK(heap.tryFree(p));
    CHECK(!heap.tryFree(p));
    CHECK(heap.getStatistics().invalidFrees == 3);
    CHECK(heap.getStatistics().freeBytes == initial.freeBytes);
    p = ::operator new(128);
    CHECK(p != 0);
    ::operator delete(p, 128);
    p = ::operator new[](128);
    CHECK(p != 0);
    ::operator delete[](p, 128);
    CHECK(::operator new(4, arena) == arena);
    CHECK(RunHeapSelfTest());
    CHECK(MemoryManager::activeMemoryManager == &heap);
    CHECK(heap.getStatistics().allocatedBlocks == 0);
    {
        MemoryManager empty(0, 100, false);
        CHECK(!empty.validate() && !empty.malloc(1));
        CHECK(empty.tryFree(0));
        MemoryManager tiny((size_t)arena, 15, false);
        CHECK(!tiny.validate());
        MemoryManager overflow(0xFFFFFFF0u, 64, false);
        CHECK(!overflow.validate());
    }
    // Invalid metadata must fail safely, without following hostile links.
    MemoryChunk* header = (MemoryChunk*)((uint8_t*)arena + 16);
    MemoryChunk* savedNext = header->next;
    header->next = (MemoryChunk*)0xFFFFFFF0;
    CHECK(!heap.validate() && !heap.malloc(16));
    header->next = savedNext;
    CHECK(heap.validate());
    const size_t savedSize = header->size;
    header->size = 0xFFFFFFF0u;
    CHECK(!heap.validate() && !heap.malloc(16));
    header->size = savedSize;
    CHECK(heap.validate());
    const uint32_t savedMagic = header->magic;
    header->magic = 0;
    CHECK(!heap.validate());
    header->magic = savedMagic;
    CHECK(heap.validate());
    ((uint8_t*)header)[__builtin_offsetof(MemoryChunk, allocated)] = 2;
    CHECK(!heap.validate());
    header->allocated = false;
    CHECK(heap.validate());
    p = heap.malloc(initial.freeBytes);
    CHECK(p != 0 && heap.malloc(1) == 0);
    CHECK(heap.getStatistics().freeBlocks == 0);
    CHECK(heap.tryFree(p));
}
static void physicalTests() {
    PhysicalMemoryManager frames;
    uint32_t address = 123;
    CHECK(!frames.allocate(address) && address == 0);
    CHECK(!frames.free(0x100000));
    CHECK(!frames.initialize(&info, 0, 0x100000, 0x180000));
    CHECK(frames.getLastError() == PhysicalMemoryBadMagic);
    CHECK(!frames.initialize(0, MultibootBootMagic, 0x100000, 0x180000));
    zero(&info, sizeof(info));
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(frames.getLastError() == PhysicalMemoryNoMap);

    // Reserved entries beat available entries even when the map is out of order.
    uint32_t length = appendMap(0, 0x300123, 1, 2, 8);
    length = appendMap(length, 0, 16 * 1024 * 1024, 1);
    length = appendMap(length, 0x100000000ULL, 16 * 1024 * 1024, 1);
    mapInfo(length);
    info.flags |= (1u << 3) | (1u << 2) | (1u << 9);
    info.commandLine = (uint32_t)command;
    info.bootLoaderName = (uint32_t)command;
    info.moduleCount = 1;
    info.modules = (uint32_t)modules;
    modules[0].start = 0x400123;
    modules[0].end = 0x403001;
    modules[0].string = (uint32_t)moduleName;
    PhysicalRange extra = { 0x500001, 4096 };
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180001, &extra, 1));
    PhysicalMemoryStatistics stats = frames.getStatistics();
    CHECK(stats.initialized && stats.usedMemoryMap);
    CHECK(RunPhysicalMemorySelfTest(frames));
    CHECK(frames.getStatistics().freeFrames == stats.freeFrames);
    CHECK(stats.addressableFrames == 4096);
    CHECK(stats.freeFrames == 4096 - 256 - 129 - 1 - 4 - 2);
    CHECK(!frames.isFree(0) && !frames.isFree(0x100000) && !frames.isFree(0x180000));
    CHECK(frames.isFree(0x181000));
    CHECK(!frames.isFree(0x300000) && !frames.isFree(0x400000) && !frames.isFree(0x403000));
    CHECK(!frames.isFree(0x500000) && !frames.isFree(0x501000));
    CHECK(frames.allocate(address) && address == 0x181000);
    CHECK(frames.isAllocated(address) && !frames.isFree(address));
    CHECK(!frames.reserveRegion(address, 1));
    CHECK(!frames.free(address + 1));
    CHECK(!frames.free(0x100000));
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180001));
    CHECK(frames.isAllocated(address));
    CHECK(frames.free(address));
    CHECK(!frames.free(address));
    CHECK(frames.getStatistics().freeFrames == stats.freeFrames);
    CHECK(frames.allocateContiguous(4, address, 16, 0xFFFFFF));
    CHECK(!(address & 0xFFFF));
    CHECK(!frames.freeContiguous(address, 5));
    CHECK(frames.isAllocated(address));
    CHECK(frames.freeContiguous(address, 4));
    CHECK(!frames.allocateContiguous(0, address));
    CHECK(!frames.allocateContiguous(1, address, 3));
    CHECK(!frames.allocateContiguous(1, address, 1, 0xFFFFF));
    CHECK(!frames.allocateContiguous(0xFFFFFFFFu, address));
    CHECK(frames.reserveRegion(0x200001, 4096));
    CHECK(!frames.isFree(0x200000) && !frames.isFree(0x201000));
    CHECK(frames.getStatistics().freeFrames == stats.freeFrames - 2);
    CHECK(!frames.reserveRegion(~0ULL, 2));

    // Malformed or empty maps must not silently fall back to mem_upper.
    mapInfo(3);
    info.flags |= 1;
    info.memUpper = 65536;
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(frames.getLastError() == PhysicalMemoryBadMap && !frames.getStatistics().initialized);
    CHECK(!frames.allocate(address));
    mapInfo(appendMap(0, 0, 0x1000000, 1));
    ((MultibootMemoryMapEntry*)mapBuffer)->size = 19;
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    mapInfo(appendMap(0, ~0ULL - 10, 20, 1));
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    mapInfo(appendMap(0, 0, 0x1000000, 1) + 1);
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    mapInfo(0);
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    mapInfo(24);
    info.memoryMap = 0xFFFFFFF0u;
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));

    // Fallback correctly adds the lower 1 MiB to mem_upper.
    zero(&info, sizeof(info));
    info.flags = 1;
    info.memUpper = 3 * 1024;
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x200000));
    CHECK(!frames.getStatistics().usedMemoryMap && frames.getStatistics().freeFrames == 512);
    CHECK(frames.allocateContiguous(512, address) && address == 0x200000);
    CHECK(!frames.allocate(address));
    CHECK(frames.getStatistics().freeFrames == 0);
    CHECK(frames.freeContiguous(0x200000, 512));

    // Partial available pages are excluded; partial reserved pages are excluded too.
    length = appendMap(0, 0x200001, 0x4FFF, 1);
    length = appendMap(length, 0x202FFF, 2, 2);
    mapInfo(length);
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(frames.getStatistics().freeFrames == 2);
    CHECK(frames.isFree(0x201000) && frames.isFree(0x204000));
    CHECK(!frames.isFree(0x200000) && !frames.isFree(0x202000) && !frames.isFree(0x203000));
    CHECK(!frames.allocateContiguous(2, address));
    CHECK(frames.allocate(address) && address == 0x201000);
    CHECK(frames.allocate(address) && address == 0x204000);
    CHECK(!frames.allocate(address));
    CHECK(frames.free(0x201000) && frames.free(0x204000));

    // A frame ending at 4 GiB is valid; memory above it is ignored.
    mapInfo(appendMap(0, 0xFFFFE000ULL, 0x4000, 1));
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(frames.getStatistics().freeFrames == 2);
    CHECK(frames.allocateContiguous(2, address) && address == 0xFFFFE000u);
    CHECK(frames.isAllocated(0xFFFFF000u));
    CHECK(frames.freeContiguous(address, 2));

    // Framebuffer overlap is permanently excluded even when firmware marks it RAM.
    mapInfo(appendMap(0, 0, 16 * 1024 * 1024, 1));
    info.flags |= 1u << 12;
    info.framebufferAddress = 0x600123;
    info.framebufferPitch = 4096;
    info.framebufferHeight = 2;
    info.framebufferType = 1;
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(!frames.isFree(0x600000) && !frames.isFree(0x601000) && !frames.isFree(0x602000));
    // Descriptor tables themselves can be inside available RAM.
    mapInfo(appendMap(0, 0, 256 * 1024 * 1024, 1));
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(!frames.isFree((uint32_t)&info & ~4095u));
    CHECK(!frames.isFree((uint32_t)mapBuffer & ~4095u));
    PhysicalMemoryManager* resident = new (physicalStorage) PhysicalMemoryManager;
    CHECK(resident->initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(!resident->isFree((uint32_t)physicalStorage & ~4095u));
    CHECK(!resident->isFree(((uint32_t)physicalStorage + sizeof(physicalStorage) - 1) & ~4095u));
    resident->~PhysicalMemoryManager();

    // Bad module and symbol metadata fail closed.
    mapInfo(appendMap(0, 0, 16 * 1024 * 1024, 1));
    info.flags |= 1u << 3;
    info.moduleCount = 1;
    info.modules = (uint32_t)modules;
    modules[0].start = 10;
    modules[0].end = 9;
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(frames.getLastError() == PhysicalMemoryBadReservation);
    info.flags = (1u << 6) | (1u << 4) | (1u << 5);
    CHECK(!frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    info.flags = (1u << 6) | (1u << 5);
    info.symbols[0] = 1;
    info.symbols[1] = 40;
    info.symbols[2] = (uint32_t)section;
    zero(section, sizeof(section));
    *(uint32_t*)(section + 12) = 0x700001;
    *(uint32_t*)(section + 20) = 0x1000;
    CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180000));
    CHECK(!frames.isFree(0x700000) && !frames.isFree(0x701000));
}
static void physicalPropertyTests() {
    PhysicalMemoryManager frames;
    uint8_t expected[4096];
    uint32_t random = 0x5EED1234;
    for (uint32_t trial = 0; trial < 128; ++trial) {
        uint32_t length = appendMap(0, 0, 16 * 1024 * 1024, 1);
        for (uint32_t i = 0; i < 7; ++i) {
            random = random * 1664525u + 1013904223u;
            const uint32_t address = random & 0xFFFFFF;
            random = random * 1664525u + 1013904223u;
            const uint32_t bytes = random & 0x1FFFF;
            length = appendMap(length, address, bytes, (random & 1) ? 1 : 2);
        }
        mapInfo(length);
        CHECK(frames.initialize(&info, MultibootBootMagic, 0x100000, 0x180001));
        uint32_t expectedCount = 0;
        for (uint32_t page = 0; page < 4096; ++page) {
            bool free = page >= 385;
            const uint32_t start = page * 4096;
            for (uint32_t i = 0; i < 8; ++i) {
                const MultibootMemoryMapEntry* entry = (const MultibootMemoryMapEntry*)(mapBuffer + 24 * i);
                if (entry->type != 1 && entry->length && entry->address < start + 4096
                    && entry->address + entry->length > start) free = false;
            }
            expected[page] = free;
            if (free) ++expectedCount;
            CHECK(frames.isFree(start) == free);
        }
        CHECK(frames.getStatistics().freeFrames == expectedCount);
        uint32_t live[16];
        for (uint32_t i = 0; i < 16; ++i) live[i] = 0;
        for (uint32_t step = 0; step < 128; ++step) {
            random = random * 1664525u + 1013904223u;
            const uint32_t slot = (random >> 16) & 15;
            if (live[slot]) {
                CHECK(frames.free(live[slot]));
                expected[live[slot] >> 12] = true;
                live[slot] = 0;
                ++expectedCount;
            } else {
                uint32_t firstFree = 0;
                while (firstFree < 4096 && !expected[firstFree]) ++firstFree;
                CHECK(frames.allocate(live[slot]));
                CHECK(live[slot] == firstFree * 4096);
                expected[firstFree] = false;
                --expectedCount;
            }
            CHECK(frames.getStatistics().freeFrames == expectedCount);
        }
        for (uint32_t i = 0; i < 16; ++i) if (live[i]) CHECK(frames.free(live[i]));
    }
}
extern "C" int memoryTests() {
    heapTests();
    physicalTests();
    physicalPropertyTests();
    print("Memory tests: "); number(checks); print(" checks, "); number(failures); print(" failures\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n xorl %ebp,%ebp\n andl $-16,%esp\n call memoryTests\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
