#include <memory/process_address_space.h>
using namespace gtos::memory;
extern "C" uint8_t __executable_start, _end;
extern "C" {
    uint32_t gtos_process_memory_test_cr0 = 0x80010000u;
    uint32_t gtos_process_memory_test_cr3 = 0;
    uint32_t gtos_process_memory_test_cr4 = 0;
    bool gtos_process_memory_test_bsp = true;
}
namespace {
    const uint32_t Arena = 0x10000000, ArenaBytes = 16 * 1024 * 1024;
    const uint32_t UserBase = ProcessAddressSpace::UserBase, UserLimit = ProcessAddressSpace::UserLimit;
    uint32_t checks = 0, failures = 0, baseline = 0;
    const uint32_t BufferAlias = 0x20000000;
    uint32_t borrowedBuffer = 0;
    static_assert(__has_trivial_destructor(ProcessAddressSpace), "live spaces must not auto-free");
    MultibootInfo info;
    MultibootMemoryMapEntry memoryMap[2];
    PhysicalMemoryManager frames, foreignFrames;
    KernelPaging kernel;
    ProcessAddressSpace first, second, spare;
    uint32_t retained[4096];
    uint8_t input[8192], output[8192];
    void print(const char* text) {
        uint32_t n = 0; while (text[n]) ++n;
        uint32_t written;
        asm volatile("int $0x80" : "=a"(written) : "0"(4), "b"(1), "c"(text), "d"(n) : "memory", "cc");
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
    uint32_t used() { return frames.getStatistics().allocatedFrames; }
    uint32_t* directory(ProcessAddressSpace& process) { return (uint32_t*)process.DirectoryAddress(); }
    uint32_t* table(ProcessAddressSpace& process, uint32_t address) {
        return (uint32_t*)(directory(process)[address >> 22] & ~4095u);
    }
    uint32_t& entry(ProcessAddressSpace& process, uint32_t address) {
        return table(process, address)[(address >> 12) & 1023];
    }
    uint32_t physical(ProcessAddressSpace& process, uint32_t address) {
        return entry(process, address) & ~4095u;
    }
    void fill(uint8_t* bytes, uint32_t length, uint8_t value) {
        for (uint32_t i = 0; i < length; ++i) bytes[i] = value;
    }
    bool equal(const uint8_t* a, const uint8_t* b, uint32_t length) {
        for (uint32_t i = 0; i < length; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    bool all(const uint8_t* bytes, uint32_t length, uint8_t value) {
        for (uint32_t i = 0; i < length; ++i) if (bytes[i] != value) return false;
        return true;
    }
    uint32_t templateHash() {
        const uint32_t* root = (uint32_t*)kernel.getStatistics().directoryAddress;
        uint32_t hash = 0x811C9DC5u;
        for (uint32_t i = 0; i < 1024; ++i) {
            hash = (hash ^ root[i]) * 0x01000193u;
            if (root[i] & 1) {
                const uint32_t* t = (const uint32_t*)(root[i] & ~4095u);
                for (uint32_t j = 0; j < 1024; ++j) hash = (hash ^ t[j]) * 0x01000193u;
            }
        }
        return hash;
    }
    void diagnostics() {
        static_assert(sizeof(ProcessMemoryError) == sizeof(uint32_t), "diagnostic enum width");
        const char expected[] = "unknown process memory error";
        const uint32_t unknown[] = {0x100u, 0x80000000u, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
            const ProcessMemoryError value = static_cast<ProcessMemoryError>(unknown[i]);
            CHECK(static_cast<uint32_t>(value) == unknown[i]);
            CHECK(equal((const uint8_t*)ProcessAddressSpace::ErrorName(value),
                (const uint8_t*)expected, sizeof(expected)));
        }
    }
    void initialize() {
        CHECK(!first.Prepared() && !first.Sealed() && !first.DirectoryAddress());
        CHECK(!first.Prepare(kernel, frames));
        CHECK(!first.MapNewPage(UserBase, true)); CHECK(!first.ProtectPage(UserBase, true));
        CHECK(!first.UnmapPage(UserBase)); CHECK(!first.Seal()); CHECK(!first.Destroy());
        CHECK(!first.ValidateUserRange(UserBase, 0, false));
        memoryMap[0].size = memoryMap[1].size = 20;
        memoryMap[0].address = Arena; memoryMap[0].length = ArenaBytes; memoryMap[0].type = 1;
        memoryMap[1].address = Arena + 0x200000; memoryMap[1].length = 4096; memoryMap[1].type = 2;
        info.flags = 1u << 6; info.memoryMap = (uint32_t)memoryMap; info.memoryMapLength = sizeof(memoryMap);
        PagingConfig config = {(uint32_t)&__executable_start, ((uint32_t)&_end + 4095) & ~4095u,
            (uint32_t)&__executable_start, (uint32_t)&__executable_start + 4096, &info, 0, 0};
        CHECK(frames.initialize(&info, MultibootBootMagic, config.kernelStart, config.kernelEnd));
        CHECK(kernel.prepareIdentity(frames, config));
        CHECK(!first.Prepare(kernel, frames)); // Unsealed, inactive template.
        CHECK(frames.allocate(borrowedBuffer));
        CHECK(kernel.mapOwnedPage(BufferAlias, borrowedBuffer, true));
        CHECK(kernel.enable());
        CHECK(!first.Prepare(kernel, frames)); // Still unsealed.
        CHECK(kernel.sealForSharedProcessors());
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        baseline = used();
    }
    void preparationAndHardware() {
        gtos_process_memory_test_cr0 = 0;
        CHECK(!first.Prepare(kernel, frames)); CHECK(used() == baseline);
        gtos_process_memory_test_cr0 = 0x80000000u;
        CHECK(!first.Prepare(kernel, frames)); // WP is essential.
        gtos_process_memory_test_cr0 = 0x80010000u;
        gtos_process_memory_test_cr4 = 1u << 5;
        CHECK(!first.Prepare(kernel, frames));
        gtos_process_memory_test_cr4 = 0;
        gtos_process_memory_test_bsp = false;
        CHECK(!first.Prepare(kernel, frames));
        gtos_process_memory_test_bsp = true;
        gtos_process_memory_test_cr3 += 4096;
        CHECK(!first.Prepare(kernel, frames));
        gtos_process_memory_test_cr3 -= 4096;
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.Prepared() && !first.Sealed() && first.DirectoryAddress());
        CHECK(used() == baseline + 1);
        CHECK(!first.Prepare(kernel, frames));
        CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    void allocatorIdentity() {
        const uint32_t kernelStart = (uint32_t)&__executable_start;
        const uint32_t kernelEnd = ((uint32_t)&_end + 4095) & ~4095u;
        CHECK(foreignFrames.initialize(&info, MultibootBootMagic, kernelStart, kernelEnd));
        // Manufacture matching allocation bits without touching the physical
        // pages. Only allocator object identity can distinguish this unsafe
        // overlapping allocator from the real owner of the kernel template.
        for (uint32_t i = 0; i < baseline; ++i) CHECK(foreignFrames.allocate(retained[i]));
        const uint32_t* root = (const uint32_t*)kernel.getStatistics().directoryAddress;
        CHECK(foreignFrames.isAllocated(kernel.getStatistics().directoryAddress));
        for (uint32_t i = 0; i < 1024; ++i)
            if (root[i] & 1) CHECK(foreignFrames.isAllocated(root[i] & ~4095u));
        CHECK(kernel.usesAllocator(frames)); CHECK(!kernel.usesAllocator(foreignFrames));
        const PhysicalMemoryStatistics before = foreignFrames.getStatistics();
        const PhysicalMemoryStatistics ownerBefore = frames.getStatistics();
        const uint32_t hash = templateHash();
        CHECK(!first.Prepare(kernel, foreignFrames));
        CHECK(first.GetLastError() == ProcessMemoryBadState);
        CHECK(!first.Prepared() && !first.Sealed() && !first.DirectoryAddress());
        const PhysicalMemoryStatistics after = foreignFrames.getStatistics();
        const PhysicalMemoryStatistics ownerAfter = frames.getStatistics();
        CHECK(after.allocatedFrames == before.allocatedFrames && after.freeFrames == before.freeFrames
            && after.failedAllocations == before.failedAllocations && after.initialized);
        CHECK(ownerAfter.allocatedFrames == ownerBefore.allocatedFrames
            && ownerAfter.freeFrames == ownerBefore.freeFrames
            && ownerAfter.failedAllocations == ownerBefore.failedAllocations);
        CHECK(templateHash() == hash);
        for (uint32_t i = 0; i < baseline; ++i) CHECK(foreignFrames.free(retained[i]));
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    void templateRejections() {
        uint32_t* root = (uint32_t*)kernel.getStatistics().directoryAddress;
        const uint32_t index = Arena >> 22, original = root[index];
        const uint32_t changes[] = {4, 0x80, 0x100, 0x400, 0x18};
        for (uint32_t i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i) {
            root[index] = original | changes[i];
            CHECK(!first.Prepare(kernel, frames)); CHECK(used() == baseline);
            root[index] = original;
        }
        root[UserBase >> 22] = original; // Even a hole in this PDE is forbidden.
        CHECK(!first.Prepare(kernel, frames));
        root[UserBase >> 22] = 0;
        root[(UserLimit >> 22) - 1] = original;
        CHECK(!first.Prepare(kernel, frames));
        root[(UserLimit >> 22) - 1] = 0;
        root[500] = 2; CHECK(!first.Prepare(kernel, frames)); root[500] = 0;
        uint32_t* entries = (uint32_t*)(original & ~4095u);
        const uint32_t saved = entries[0];
        const uint32_t badPtes[] = {4, 0x80, 0x100, 0x400};
        for (uint32_t i = 0; i < sizeof(badPtes) / sizeof(badPtes[0]); ++i) {
            entries[0] = saved | badPtes[i]; CHECK(!first.Prepare(kernel, frames)); entries[0] = saved;
        }
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    void isolationAndCopies() {
        const uint32_t hash = templateHash();
        CHECK(first.Prepare(kernel, frames)); CHECK(second.Prepare(kernel, frames));
        CHECK(first.DirectoryAddress() != second.DirectoryAddress());
        CHECK(first.MapNewPage(UserBase, true)); CHECK(second.MapNewPage(UserBase, true));
        CHECK(physical(first, UserBase) != physical(second, UserBase));
        CHECK(all((uint8_t*)physical(first, UserBase), 4096, 0));
        CHECK(!first.MapNewPage(UserBase, true)); CHECK(first.GetLastError() == ProcessMemoryConflict);
        const uint32_t invalidPages[] = {0, 4096, UserBase - 4096, UserBase + 1, UserLimit, 0xFFFFF000u};
        for (uint32_t i = 0; i < sizeof(invalidPages) / sizeof(invalidPages[0]); ++i) {
            CHECK(!first.MapNewPage(invalidPages[i], true));
            CHECK(!first.ProtectPage(invalidPages[i], false)); CHECK(!first.UnmapPage(invalidPages[i]));
        }
        CHECK(!first.ValidateUserRange(0, 0, false)); CHECK(!first.ValidateUserRange(UserLimit, 0, false));
        CHECK(first.ValidateUserRange(UserBase + 0x400000, 0, true));
        CHECK(!first.ValidateUserRange(UserBase, 0xFFFFFFFFu, false));
        CHECK(!first.ValidateUserRange(UserLimit - 1, 2, false));
        CHECK(!first.ValidateUserRange(Arena, 1, false)); // Supervisor identity pointer.
        CHECK(first.CopyToUser(UserBase, 0, 0)); CHECK(first.CopyFromUser(0, UserBase, 0));
        for (uint32_t i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)(i * 17 + 3);
        fill((uint8_t*)BufferAlias, 64, 0x38);
        CHECK(first.CopyToUser(UserBase, (void*)BufferAlias, 64));
        CHECK(first.CopyFromUser(output, UserBase, 64)); CHECK(all(output, 64, 0x38));
        CHECK(first.CopyToUser(UserBase, input, 64));
        CHECK(first.CopyFromUser((void*)BufferAlias, UserBase, 64));
        CHECK(equal(input, (uint8_t*)BufferAlias, 64));
        CHECK(first.CopyToUser(UserBase, input, 4096));
        fill(output, sizeof(output), 0);
        CHECK(first.CopyFromUser(output, UserBase, 4096)); CHECK(equal(input, output, 4096));
        CHECK(all((uint8_t*)physical(second, UserBase), 4096, 0));
        fill(output, sizeof(output), 0xA7);
        CHECK(!first.CopyFromUser(output, UserBase + 4080, 32)); CHECK(all(output, sizeof(output), 0xA7));
        fill((uint8_t*)physical(first, UserBase), 4096, 0x69);
        CHECK(!first.CopyToUser(UserBase + 4080, input, 32));
        CHECK(all((uint8_t*)physical(first, UserBase), 4096, 0x69));
        CHECK(first.MapNewPage(UserBase + 4096, false));
        CHECK(first.ValidateUserRange(UserBase + 4096, 4096, false));
        CHECK(!first.ValidateUserRange(UserBase + 4096, 1, true));
        CHECK(!first.CopyToUser(UserBase + 4080, input, 32));
        CHECK(all((uint8_t*)physical(first, UserBase), 4096, 0x69));
        CHECK(first.CopyFromUser(output, UserBase + 4080, 32));
        CHECK(all(output, 16, 0x69) && all(output + 16, 16, 0));
        CHECK(first.ProtectPage(UserBase + 4096, true));
        CHECK(first.CopyToUser(UserBase + 4000, input, 4192));
        CHECK(first.CopyFromUser(output, UserBase + 4000, 4192)); CHECK(equal(input, output, 4192));
        CHECK(!first.CopyFromUser(&__executable_start, UserBase, 1));
        CHECK(!first.CopyToUser(UserBase, (const void*)(Arena + 0x200000), 1)); // Unmapped physical hole.
        const uint32_t edge = Arena + 0x200000 - 16;
        fill((uint8_t*)edge, 16, 0x72);
        CHECK(!first.CopyFromUser((void*)edge, UserBase, 32)); CHECK(all((uint8_t*)edge, 16, 0x72));
        fill((uint8_t*)physical(first, UserBase), 32, 0x19);
        CHECK(!first.CopyToUser(UserBase, (void*)edge, 32));
        CHECK(all((uint8_t*)physical(first, UserBase), 32, 0x19));
        CHECK(!first.CopyToUser(UserBase, (const void*)UserBase, 1));
        CHECK(!first.CopyFromUser((void*)UserBase, UserBase, 1));
        CHECK(!first.CopyToUser(UserBase, (const void*)0xFFFFFFF0u, 32));
        CHECK(!first.CopyFromUser((void*)0xFFFFFFF0u, UserBase, 32));
        const uint32_t own[] = {physical(first, UserBase), first.DirectoryAddress(),
            (uint32_t)table(first, UserBase), kernel.getStatistics().directoryAddress,
            ((uint32_t*)kernel.getStatistics().directoryAddress)[Arena >> 22] & ~4095u};
        for (uint32_t i = 0; i < sizeof(own) / sizeof(own[0]); ++i) {
            CHECK(!first.CopyToUser(UserBase, (void*)own[i], 1));
            CHECK(!first.CopyFromUser((void*)own[i], UserBase, 1));
        }
        // A nonidentity kernel alias cannot disguise overlap with owned data.
        uint32_t* root = (uint32_t*)kernel.getStatistics().directoryAddress;
        uint32_t& alias = ((uint32_t*)(root[BufferAlias >> 22] & ~4095u))[(BufferAlias >> 12) & 1023];
        const uint32_t aliasSaved = alias;
        alias = physical(first, UserBase) | 3;
        CHECK(!first.CopyToUser(UserBase, (void*)BufferAlias, 1));
        CHECK(!first.CopyFromUser((void*)BufferAlias, UserBase, 1));
        alias = aliasSaved;
        CHECK(first.MapNewPage(UserLimit - 4096, true));
        CHECK(first.CopyToUser(UserLimit - 1, input, 1));
        CHECK(first.CopyFromUser(output, UserLimit - 1, 1)); CHECK(output[0] == input[0]);
        CHECK(!first.ProtectPage(UserBase + 0x400000, true));
        CHECK(!first.UnmapPage(UserBase + 0x400000));
        CHECK(first.UnmapPage(UserLimit - 4096));
        CHECK(!directory(first)[(UserLimit - 1) >> 22]); // Last page releases its table.
        CHECK(first.Destroy()); CHECK(second.Destroy());
        CHECK(used() == baseline && templateHash() == hash);
    }
    void corruptedMappings() {
        CHECK(first.Prepare(kernel, frames)); CHECK(first.MapNewPage(UserBase, true));
        CHECK(second.Prepare(kernel, frames)); CHECK(second.MapNewPage(UserBase, true));
        const uint32_t count = used(), original = entry(first, UserBase);
        const uint32_t invalid[] = {original & ~1u, original & ~4u, original & ~2u,
            original | 0x80, original | 0x100, original | 0x200, original | 0x18,
            (original & 4095) | physical(second, UserBase),
            (original & 4095) | kernel.getStatistics().directoryAddress};
        for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            entry(first, UserBase) = invalid[i]; fill(output, 32, 0x51);
            CHECK(!first.CopyFromUser(output, UserBase, 32)); CHECK(all(output, 32, 0x51));
            CHECK(!first.MapNewPage(UserBase + 4096, true)); CHECK(!first.Destroy()); CHECK(used() == count);
            entry(first, UserBase) = original;
        }
        uint32_t& pde = directory(first)[UserBase >> 22]; const uint32_t saved = pde;
        const uint32_t pdeBad[] = {saved & ~4u, saved & ~2u, saved | 0x80, saved | 0x100, saved | 0x40,
            kernel.getStatistics().directoryAddress | 7};
        for (uint32_t i = 0; i < sizeof(pdeBad) / sizeof(pdeBad[0]); ++i) {
            pde = pdeBad[i]; CHECK(!first.ValidateUserRange(UserBase, 1, false));
            CHECK(!first.Destroy()); CHECK(used() == count); pde = saved;
        }
        directory(first)[(UserBase >> 22) + 1] = 0x20;
        CHECK(!first.Seal()); CHECK(!first.ValidateUserRange(UserBase, 1, false));
        directory(first)[(UserBase >> 22) + 1] = 0;
        uint32_t* shared = (uint32_t*)kernel.getStatistics().directoryAddress;
        const uint32_t data = physical(first, UserBase);
        uint32_t& dataAlias = ((uint32_t*)(shared[data >> 22] & ~4095u))[(data >> 12) & 1023];
        const uint32_t aliasSaved = dataAlias; dataAlias = 0;
        fill(output, 32, 0x17);
        CHECK(!first.CopyFromUser(output, UserBase, 32)); CHECK(all(output, 32, 0x17));
        CHECK(!first.Destroy()); CHECK(used() == count); dataAlias = aliasSaved;
        table(first, UserBase)[1] = original;
        CHECK(!first.Seal()); CHECK(!first.Destroy()); table(first, UserBase)[1] = 0;
        table(first, UserBase)[1] = 2;
        CHECK(!first.Seal()); table(first, UserBase)[1] = 0;
        entry(first, UserBase) |= 0x60; pde |= 0x20; // Real hardware A/D changes are valid.
        CHECK(first.ValidateUserRange(UserBase, 4096, true));
        CHECK(first.Destroy()); CHECK(second.Destroy()); CHECK(used() == baseline);
    }
    void exhaustionAndAliases() {
        CHECK(first.Prepare(kernel, frames));
        uint32_t held = 0, frame = 0;
        while (frames.allocate(frame)) retained[held++] = frame;
        uint32_t count = used();
        CHECK(!second.Prepare(kernel, frames)); CHECK(second.GetLastError() == ProcessMemoryNoMemory);
        CHECK(!second.Prepared() && !second.DirectoryAddress()); CHECK(used() == count);
        CHECK(!first.MapNewPage(UserBase, true)); CHECK(used() == count);
        CHECK(frames.free(retained[--held])); --count; // Table succeeds, data allocation fails.
        CHECK(!first.MapNewPage(UserBase, true)); CHECK(used() == count);
        CHECK(!directory(first)[UserBase >> 22]);
        CHECK(frames.free(retained[--held])); --count;
        CHECK(first.MapNewPage(UserBase, true)); CHECK(used() == count + 2);
        CHECK(!first.MapNewPage(UserBase + 4096, true)); CHECK(used() == count + 2);
        while (held) CHECK(frames.free(retained[--held]));
        CHECK(first.Destroy()); CHECK(used() == baseline);
        // Simulate a newly allocated frame lacking its promised identity alias.
        // Alter and restore the actual template PTE while no process is exposed.
        CHECK(frames.allocate(frame)); CHECK(frames.free(frame));
        uint32_t* root = (uint32_t*)kernel.getStatistics().directoryAddress;
        uint32_t& pte = ((uint32_t*)(root[frame >> 22] & ~4095u))[(frame >> 12) & 1023];
        const uint32_t saved = pte; pte = 0;
        CHECK(!first.Prepare(kernel, frames)); CHECK(first.GetLastError() == ProcessMemoryBadAlias);
        CHECK(used() == baseline); pte = saved;
        CHECK(first.Prepare(kernel, frames));
        CHECK(frames.allocate(frame)); CHECK(frames.free(frame));
        uint32_t& tableAlias = ((uint32_t*)(root[frame >> 22] & ~4095u))[(frame >> 12) & 1023];
        const uint32_t aliasSaved = tableAlias; tableAlias = 0;
        CHECK(!first.MapNewPage(UserBase, true)); CHECK(first.GetLastError() == ProcessMemoryBadAlias);
        CHECK(used() == baseline + 1); tableAlias = aliasSaved;
        CHECK(first.MapNewPage(UserBase, true));
        CHECK(frames.allocate(frame)); CHECK(frames.free(frame));
        uint32_t& dataAlias = ((uint32_t*)(root[frame >> 22] & ~4095u))[(frame >> 12) & 1023];
        const uint32_t dataSaved = dataAlias; dataAlias = 0;
        CHECK(!first.MapNewPage(UserBase + 4096, true)); CHECK(first.GetLastError() == ProcessMemoryBadAlias);
        CHECK(used() == baseline + 3); dataAlias = dataSaved;
        // New-table allocation succeeds, then its data frame has a bad alias.
        uint32_t tableCandidate = 0, dataCandidate = 0;
        CHECK(frames.allocate(tableCandidate)); CHECK(frames.allocate(dataCandidate));
        CHECK(frames.free(tableCandidate)); CHECK(frames.free(dataCandidate));
        uint32_t& nextAlias = ((uint32_t*)(root[dataCandidate >> 22] & ~4095u))[(dataCandidate >> 12) & 1023];
        const uint32_t nextSaved = nextAlias; nextAlias = 0;
        CHECK(!first.MapNewPage(UserBase + 0x400000, true));
        CHECK(first.GetLastError() == ProcessMemoryBadAlias);
        CHECK(used() == baseline + 3 && !directory(first)[(UserBase >> 22) + 1]);
        nextAlias = nextSaved;
        CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    void lifetimeAndLimits() {
        const uint32_t hash = templateHash();
        CHECK(first.Prepare(kernel, frames));
        for (uint32_t i = 0; i < ProcessAddressSpace::MaximumPages; ++i)
            CHECK(first.MapNewPage(UserBase + i * 4096, true));
        const uint32_t count = used();
        CHECK(!first.MapNewPage(UserBase + 0x400000, true)); CHECK(first.GetLastError() == ProcessMemoryLimit);
        CHECK(used() == count);
        CHECK(first.UnmapPage(UserBase));
        CHECK(first.MapNewPage(UserBase + 0x400000, true)); // Capacity is reusable.
        CHECK(first.Seal()); CHECK(first.Sealed()); CHECK(first.Seal());
        CHECK(!first.MapNewPage(UserLimit - 4096, true)); CHECK(first.GetLastError() == ProcessMemorySealed);
        CHECK(!first.UnmapPage(UserBase + 4096)); CHECK(!first.ProtectPage(UserBase + 4096, false));
        CHECK(first.CopyToUser(UserBase + 4096, input, 32));
        CHECK(first.CopyFromUser(output, UserBase + 4096, 32)); CHECK(equal(input, output, 32));
        gtos_process_memory_test_cr3 = first.DirectoryAddress();
        CHECK(first.CopyToUser(UserBase + 4096, input, 32));
        CHECK(first.ValidateUserRange(UserBase + 4096, 32, true));
        CHECK(!first.Destroy()); CHECK(first.GetLastError() == ProcessMemoryActive);
        CHECK(!first.Seal()); CHECK(!first.MapNewPage(UserLimit - 4096, true));
        CHECK(!spare.Prepare(kernel, frames));
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(second.Prepare(kernel, frames));
        gtos_process_memory_test_cr3 = second.DirectoryAddress();
        CHECK(!first.CopyFromUser(output, UserBase + 4096, 1));
        CHECK(!first.Destroy()); CHECK(first.GetLastError() == ProcessMemoryUnsafeContext);
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        gtos_process_memory_test_bsp = false;
        CHECK(!first.CopyToUser(UserBase + 4096, input, 1)); CHECK(!first.Destroy());
        CHECK(!second.MapNewPage(UserBase, true));
        gtos_process_memory_test_bsp = true;
        CHECK(second.Destroy()); CHECK(first.Destroy());
        CHECK(!first.Prepared() && !first.Sealed() && !first.DirectoryAddress());
        CHECK(!first.Destroy()); CHECK(used() == baseline && templateHash() == hash);
        CHECK(first.Prepare(kernel, frames)); CHECK(first.MapNewPage(UserBase, true));
        CHECK(all((uint8_t*)physical(first, UserBase), 4096, 0));
        CHECK(first.Destroy()); CHECK(used() == baseline);
    }
}
extern "C" int processMemoryTests() {
    uint32_t args[] = {Arena, ArenaBytes, 3, 0x32, 0xFFFFFFFF, 0};
    uint32_t address;
    asm volatile("int $0x80" : "=a"(address) : "a"(90), "b"(args) : "memory");
    if (address != Arena) { print("FAIL: mmap arena\n"); return 1; }
    args[0] = BufferAlias; args[1] = 4096;
    asm volatile("int $0x80" : "=a"(address) : "a"(90), "b"(args) : "memory");
    if (address != BufferAlias) { print("FAIL: mmap kernel buffer alias\n"); return 1; }
    diagnostics(); initialize(); preparationAndHardware(); allocatorIdentity(); templateRejections(); isolationAndCopies();
    corruptedMappings(); exhaustionAndAliases(); lifetimeAndLimits();
    print("Process memory tests: "); number(checks); print(" checks, "); number(failures); print(" failures\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n xorl %ebp,%ebp\n andl $-16,%esp\n call processMemoryTests\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
