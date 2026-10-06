#include <memory/paging.h>
using namespace gtos::memory;
extern "C" uint8_t __executable_start, _end;
namespace {
    const uint32_t Arena = 0x10000000, ArenaBytes = 16 * 1024 * 1024;
    uint32_t checks = 0, failures = 0;
    MultibootInfo info;
    MultibootMemoryMapEntry memoryMap[2];
    MultibootModule module;
    PhysicalMemoryManager frames;
    KernelPaging paging;
    void print(const char* text) {
        uint32_t n = 0; while (text[n]) ++n;
        { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4), "b"(1), "c"(text), "d"(n)  : "memory", "cc"); }
    }
    void number(uint32_t value) {
        char digits[11]; uint32_t i = 10; digits[i] = 0;
        do { digits[--i] = '0' + value % 10; value /= 10; } while (value);
        print(digits + i);
    }
    void check(bool ok, uint32_t line) {
        ++checks; if (!ok) { ++failures; print("FAIL line "); number(line); print("\n"); }
    }
#define CHECK(value) check((value), __LINE__)
    PagingConfig config() {
        PagingConfig c = {(uint32_t)&__executable_start,
            ((uint32_t)&_end + 4095) & ~4095u, (uint32_t)&__executable_start,
            (uint32_t)&__executable_start + 4096, &info, 0, 0};
        return c;
    }
    bool initialize(uint32_t bytes = ArenaBytes, bool includeModule = true) {
        for (uint32_t i = 0; i < sizeof(info); ++i) ((uint8_t*)&info)[i] = 0;
        memoryMap[0].size = memoryMap[1].size = 20;
        memoryMap[0].address = Arena; memoryMap[0].length = bytes; memoryMap[0].type = 1;
        memoryMap[1].address = Arena + 0x200000; memoryMap[1].length = 4096; memoryMap[1].type = 2;
        info.flags = 1u << 6;
        info.memoryMap = (uint32_t)memoryMap; info.memoryMapLength = sizeof(memoryMap);
        if (includeModule) {
            module.start = Arena + 0x100000; module.end = module.start + 4097;
            module.string = module.reserved = 0;
            info.flags |= 1u << 3; info.modules = (uint32_t)&module; info.moduleCount = 1;
        }
        PagingConfig c = config();
        return frames.initialize(&info, MultibootBootMagic, c.kernelStart, c.kernelEnd);
    }
    void ownershipAndBounds() {
        CHECK(initialize());
        PagingConfig c = config();
        PagingDeviceRange devices[] = {{0xA0000, 0x20000}, {0xFEE00000, 4096}};
        c.devices = devices; c.deviceCount = 2;
        uint32_t initial = frames.getStatistics().allocatedFrames;
        CHECK(paging.prepareIdentity(frames, c));
        PagingStatistics ps = paging.getStatistics();
        CHECK(ps.prepared && !ps.enabled && ps.directoryAddress && ps.pageTableFrames >= 6);
        CHECK(frames.getStatistics().allocatedFrames == initial + ps.pageTableFrames + 1);
        CHECK(!paging.prepareIdentity(frames, c));
        PagingMapping mapping;
        CHECK(!paging.query(0, mapping)); CHECK(!mapping.physicalAddress && !mapping.writable);
        CHECK(!paging.query(0xFFF, mapping));
        CHECK(!paging.query(Arena + 0x200000, mapping));
        CHECK(!paging.query(0xFEC00000, mapping)); // No implicit IOAPIC mapping.
        CHECK(paging.query(c.readOnlyStart + 27, mapping));
        CHECK(mapping.physicalAddress == c.readOnlyStart + 27 && !mapping.writable && !mapping.userAccessible);
        CHECK(paging.query(module.start + 4096, mapping) && mapping.writable);
        CHECK(paging.query(0xA0000, mapping) && mapping.cacheDisabled && mapping.writable);
        CHECK(paging.query(0xFEE00000, mapping) && mapping.cacheDisabled);
        CHECK(!paging.unmapOwnedPage(c.readOnlyStart));
        CHECK(!paging.protectOwnedPage(c.readOnlyStart, true));
        uint32_t page = 0; CHECK(frames.allocate(page));
        CHECK(!paging.mapOwnedPage(0, page, true));
        CHECK(!paging.mapOwnedPage(0xC0000001, page, true));
        CHECK(!paging.mapOwnedPage(0xC0000000, page + 1, true));
        CHECK(!paging.mapOwnedPage(0xC0000000, c.readOnlyStart, true));
        CHECK(!paging.mapOwnedPage(0xC0000000, ps.directoryAddress, true));
        uint32_t table = ((uint32_t*)ps.directoryAddress)[page >> 22] & ~4095u;
        CHECK(!paging.mapOwnedPage(0xC0000000, table, true));
        CHECK(!paging.mapOwnedPage(0xC0000000, Arena + ArenaBytes - 4096, true)); // Free RAM.
        CHECK(paging.mapOwnedPage(0xC0000000, page, true));
        CHECK(paging.query(0xC000002A, mapping));
        CHECK(mapping.physicalAddress == page + 42 && mapping.writable && mapping.managed && !mapping.userAccessible);
        uint32_t allocated = frames.getStatistics().allocatedFrames;
        CHECK(!paging.mapOwnedPage(0xC0000000, page, false));
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        CHECK(paging.protectOwnedPage(0xC0000000, false));
        CHECK(paging.query(0xC0000000, mapping) && !mapping.writable);
        CHECK(paging.unmapOwnedPage(0xC0000000));
        CHECK(!paging.query(0xC0000000, mapping));
        CHECK(!paging.unmapOwnedPage(0xC0000000));
        CHECK(paging.mapOwnedPage(0xFFFFF000, page, false));
        CHECK(paging.query(0xFFFFFFFF, mapping) && mapping.physicalAddress == page + 4095);
        CHECK(paging.unmapOwnedPage(0xFFFFF000));
        CHECK(frames.free(page));
        CHECK(paging.abandon());
        CHECK(frames.getStatistics().allocatedFrames == initial);
        CHECK(!paging.query(c.kernelStart, mapping));
        CHECK(!paging.mapOwnedPage(0xC0000000, page, true));
        CHECK(!paging.enable());
        CHECK(!paging.sealForSharedProcessors());
    }
    void failureRollback() {
        CHECK(initialize());
        PagingConfig c = config();
        uint32_t retained; CHECK(frames.allocate(retained));
        const uint32_t allocated = frames.getStatistics().allocatedFrames;
        PagingDeviceRange bad[] = {{0xFEE00001, 4096}, {0xFFFFF000, 8192},
            {0, 4096}, {0xA0000, 4097}, {c.kernelStart, 4096}, {Arena, 4096}};
        for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            c.devices = &bad[i]; c.deviceCount = 1;
            CHECK(!paging.prepareIdentity(frames, c));
            CHECK(!paging.getStatistics().prepared && !paging.getStatistics().directoryAddress);
            CHECK(frames.getStatistics().allocatedFrames == allocated && frames.isAllocated(retained));
        }
        c = config(); c.readOnlyEnd = c.readOnlyStart + 1;
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        c = config(); c.deviceCount = 65;
        CHECK(!paging.prepareIdentity(frames, c));
        c = config(); c.bootInfo = 0;
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        c = config(); module.end = module.start - 1;
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        module.end = module.start + 4097;
        CHECK(frames.free(retained));
        // One frame permits a directory but not a table; both must be reclaimed.
        CHECK(initialize(4096, false));
        c = config();
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(paging.getLastError() == PagingNoMemory);
        CHECK(frames.getStatistics().allocatedFrames == 0 && frames.getStatistics().freeFrames == 1);
        // Fail after several PDEs have been created, not just the first one.
        CHECK(initialize(5 * 4096, false));
        PagingDeviceRange distant[] = {{0xA0000, 4096}, {0x40000000, 4096}, {0x80000000, 4096}, {0xFEE00000, 4096}};
        c = config(); c.devices = distant; c.deviceCount = 4;
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(paging.getLastError() == PagingNoMemory);
        CHECK(frames.getStatistics().allocatedFrames == 0 && frames.getStatistics().freeFrames == 5);
        CHECK(initialize());
        c = config();
        CHECK(paging.prepareIdentity(frames, c));
        // Fill all free RAM, then demand a new page table. Existing mapping and
        // allocator state must survive the failed operation.
        uint32_t data; CHECK(frames.allocate(data));
        uint32_t live[4096], used = 0, frame;
        while (frames.allocate(frame)) live[used++] = frame;
        PagingStatistics before = paging.getStatistics();
        CHECK(!paging.mapOwnedPage(0xC0000000, data, true));
        CHECK(paging.getLastError() == PagingNoMemory);
        CHECK(paging.getStatistics().mappedPages == before.mappedPages);
        CHECK(paging.getStatistics().pageTableFrames == before.pageTableFrames);
        while (used) CHECK(frames.free(live[--used]));
        CHECK(frames.free(data)); CHECK(paging.abandon());
    }
    void allocatorPlacement() {
        CHECK(initialize());
        // The allocator itself is a boot reservation, not ordinary free RAM.
        // Place a second allocator on the host stack, outside the linked image.
        PhysicalMemoryManager external;
        PagingConfig c = config();
        CHECK(external.initialize(&info, MultibootBootMagic, c.kernelStart, c.kernelEnd));
        CHECK(paging.prepareIdentity(external, c));
        PagingMapping mapping;
        uint32_t first = (uint32_t)&external, last = first + sizeof(external) - 1;
        CHECK(first > c.kernelEnd);
        CHECK(paging.query(first, mapping) && mapping.writable && mapping.physicalAddress == first);
        CHECK(paging.query(last, mapping) && mapping.writable && mapping.physicalAddress == last);
        CHECK(paging.abandon());
        CHECK(external.getStatistics().allocatedFrames == 0);
    }
    void activeLifetime() {
        CHECK(initialize());
        PagingConfig c = config(); CHECK(paging.prepareIdentity(frames, c));
        CHECK(!paging.getStatistics().sealedForSharing);
        uint32_t page; CHECK(frames.allocate(page));
        CHECK(paging.mapOwnedPage(0xC0000000, page, true));
        CHECK(paging.sealForSharedProcessors());
        CHECK(paging.getStatistics().sealedForSharing);
        const uint32_t allocated = frames.getStatistics().allocatedFrames;
        PagingStatistics before = paging.getStatistics();
        CHECK(!paging.abandon()); CHECK(paging.getLastError() == PagingSealed);
        CHECK(!paging.mapOwnedPage(0xD0000000, page, true));
        CHECK(paging.getLastError() == PagingSealed);
        CHECK(!paging.unmapOwnedPage(0xC0000000));
        CHECK(!paging.protectOwnedPage(0xC0000000, false));
        PagingMapping mapping;
        CHECK(paging.query(0xC0000000, mapping) && mapping.writable);
        CHECK(paging.getStatistics().mappedPages == before.mappedPages);
        CHECK(paging.getStatistics().pageTableFrames == before.pageTableFrames);
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        CHECK(paging.sealForSharedProcessors()); // Idempotent one-way operation.
        CHECK(paging.enable()); // Hardware instruction path is separately QEMU-tested.
        CHECK(paging.getStatistics().enabled && paging.getStatistics().sealedForSharing);
        CHECK(!paging.abandon()); CHECK(!paging.enable());
        CHECK(!paging.prepareIdentity(frames, c));
        CHECK(!paging.mapOwnedPage(0xD0000000, page, true));
        CHECK(!paging.unmapOwnedPage(0xC0000000));
        CHECK(!paging.protectOwnedPage(0xC0000000, false));
        CHECK(frames.getStatistics().allocatedFrames == allocated);
        // The sealed data alias cannot be removed: its borrowed frame must stay
        // allocated, just like the directory/tables an AP could still be using.
    }
}
extern "C" int pagingTests() {
    uint32_t args[] = {Arena, ArenaBytes, 3, 0x32, 0xFFFFFFFF, 0};
    uint32_t address;
    asm volatile("int $0x80" : "=a"(address) : "a"(90), "b"(args) : "memory");
    if (address != Arena) { print("FAIL: mmap arena\n"); return 1; }
    ownershipAndBounds(); failureRollback(); allocatorPlacement(); activeLifetime();
    print("Paging tests: "); number(checks); print(" checks, "); number(failures); print(" failures\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n xorl %ebp,%ebp\n andl $-16,%esp\n call pagingTests\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
