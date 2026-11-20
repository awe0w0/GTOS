#include <memory/process_address_space.h>
using namespace gtos::memory;
extern "C" uint8_t __executable_start, _end;
extern "C" {
    uint32_t gtos_process_memory_test_cr0 = 0x80010000u;
    uint32_t gtos_process_memory_test_cr3 = 0;
    uint32_t gtos_process_memory_test_cr4 = 0;
    bool gtos_process_memory_test_bsp = true;
    uint32_t gtos_process_memory_test_invalidations = 0;
    uint32_t gtos_process_memory_test_invalidated[512];
    // The ProtectRange witnesses. `src/memory/process_address_space.cpp` declares all four whenever
    // GTOS_PROCESS_MEMORY_TEST is defined, so a binary that compiles that unit and links neither this file nor the runtime
    // boundary unit cannot link at all. This binary owns exactly one definition of each; the runtime host test defines the
    // same four in tests/runtime_boundary.cpp, and no binary compiles both, so there is no duplicate definition either way.
    uint32_t gtos_process_memory_test_fail_protect_after = 0;
    uint32_t gtos_process_memory_test_protect_calls = 0;
    uint32_t gtos_process_memory_test_protect_refusals = 0;
    uint32_t gtos_process_memory_test_protect_pages_at_refusal = 0;
    void gtos_process_memory_test_invalidate(uint32_t address) {
        if (gtos_process_memory_test_invalidations < 512)
            gtos_process_memory_test_invalidated[gtos_process_memory_test_invalidations] = address;
        ++gtos_process_memory_test_invalidations;
    }
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
    KernelPaging kernel, paeKernel;
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
        CHECK(frames.initialize(&info, MultibootBootMagic, config.kernelStart, config.kernelEnd, 0, 0, 40));
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
        const uint32_t aliasSaved = dataAlias;
        const uint32_t badAliases[] = {0, aliasSaved & ~2u, aliasSaved | 4u,
            aliasSaved | 0x10, (aliasSaved & 4095) | physical(second, UserBase)};
        for (uint32_t invalidAlias : badAliases) {
            dataAlias = invalidAlias;
            fill(output, 32, 0x17);
            CHECK(!first.CopyFromUser(output, UserBase, 32)); CHECK(all(output, 32, 0x17));
            CHECK(!first.Destroy()); CHECK(used() == count);
            dataAlias = aliasSaved;
        }
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
    const uint32_t DynamicBase = ProcessAddressSpace::DynamicBase;
    const uint32_t DynamicLimit = ProcessAddressSpace::DynamicLimit;
    uint32_t processHash(ProcessAddressSpace& process) {
        uint32_t hash = 0x811C9DC5u;
        const uint32_t* root = directory(process);
        for (uint32_t i = 0; i < 1024; ++i) {
            hash = (hash ^ root[i]) * 0x01000193u;
            if (i >= UserBase >> 22 && i < UserLimit >> 22 && (root[i] & 1)) {
                const uint32_t* entries = (const uint32_t*)(root[i] & ~4095u);
                for (uint32_t j = 0; j < 1024; ++j) hash = (hash ^ entries[j]) * 0x01000193u;
            }
        }
        return hash;
    }
    ProcessMemoryRegionInfo region(ProcessAddressSpace& process, uint32_t handle) {
        ProcessMemoryRegionInfo info = {};
        CHECK(process.QueryRegion(handle, info)); return info;
    }
    void sameRegion(ProcessAddressSpace& process, uint32_t handle, const ProcessMemoryRegionInfo& before) {
        const ProcessMemoryRegionInfo after = region(process, handle);
        CHECK(after.base == before.base && after.bytes == before.bytes && after.residentPages == before.residentPages);
    }
    void reserveError(uint32_t bytes, uint32_t alignment, uint32_t hint, ProcessMemoryError error) {
        const uint32_t count = used(), hash = processHash(first), invalidations = gtos_process_memory_test_invalidations;
        uint32_t base = 0xA55A5AA5u, handle = 0x5AA5A55Au;
        CHECK(!first.Reserve(bytes, alignment, hint, base, handle)); CHECK(first.GetLastError() == error);
        CHECK(base == 0xA55A5AA5u && handle == 0x5AA5A55Au);
        CHECK(used() == count && processHash(first) == hash);
        CHECK(gtos_process_memory_test_invalidations == invalidations);
    }
    void invalidHandle(ProcessAddressSpace& process, uint32_t handle) {
        ProcessMemoryRegionInfo out; fill((uint8_t*)&out, sizeof(out), 0xA5);
        const ProcessMemoryRegionInfo before = out;
        const uint32_t count = used(), hash = processHash(process);
        CHECK(!process.QueryRegion(handle, out)); CHECK(process.GetLastError() == ProcessMemoryBadState);
        CHECK(equal((const uint8_t*)&out, (const uint8_t*)&before, sizeof(out)));
        CHECK(!process.SetPermissions(handle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(!process.Decommit(handle, 0, 4096)); CHECK(!process.Discard(handle, 0, 4096));
        CHECK(!process.Trim(handle, 4096)); CHECK(!process.Release(handle));
        CHECK(used() == count && processHash(process) == hash);
    }
    void dynamicRegionsAndPermissions() {
        const uint32_t shared = templateHash();
        CHECK(first.Prepare(kernel, frames)); CHECK(first.MapNewPage(UserBase, true));
        CHECK(first.MapNewPage(DynamicBase + 4096, true));
        uint32_t base = 123, handle = 456;
        CHECK(!first.Reserve(4096, 4096, 0, base, handle)); CHECK(base == 123 && handle == 456);
        CHECK(first.Seal()); const uint32_t initial = used();
        const uint32_t badSizes[] = {0, 1, 4095, 4097, 0x80000000u, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(badSizes) / sizeof(badSizes[0]); ++i)
            reserveError(badSizes[i], 4096, 0, ProcessMemoryBadRange);
        const uint32_t badAlignments[] = {0, 1, 4095, 8193, 12288, 0x80000001u, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(badAlignments) / sizeof(badAlignments[0]); ++i)
            reserveError(4096, badAlignments[i], 0, ProcessMemoryBadRange);
        const uint32_t badHints[] = {UserBase, DynamicBase + 1, DynamicLimit, UserLimit - 4096, 0xFFFFF000u};
        for (uint32_t i = 0; i < sizeof(badHints) / sizeof(badHints[0]); ++i)
            reserveError(4096, 4096, badHints[i], ProcessMemoryBadRange);
        reserveError(8192, 4096, DynamicLimit - 4096, ProcessMemoryBadRange);
        reserveError(4096, 65536, DynamicBase + 4096, ProcessMemoryBadRange);
        reserveError(3 * 4096, 4096, DynamicBase, ProcessMemoryConflict); // Static page, not just another region.
        CHECK(first.Reserve(16 * 1024 * 1024, 65536, 0x81000000u, base, handle));
        CHECK(base == 0x81000000u && handle && handle <= 0x7FFFFFFFu && used() == initial);
        ProcessMemoryRegionInfo info = region(first, handle);
        CHECK(info.base == base && info.bytes == 16 * 1024 * 1024 && !info.residentPages);
        reserveError(4096, 4096, base + 4096, ProcessMemoryConflict); sameRegion(first, handle, info);
        uint32_t aligned = 0, otherHandle = 0;
        CHECK(first.Reserve(4096, 65536, 0, aligned, otherHandle));
        CHECK(!(aligned & 65535) && aligned >= DynamicBase && aligned < DynamicLimit);
        CHECK(otherHandle > handle && used() == initial); CHECK(first.Release(otherHandle));
        CHECK(first.SetPermissions(handle, 0, 3 * 4096, ProcessMemoryReadWrite));
        CHECK(used() == initial + 4); info = region(first, handle); CHECK(info.residentPages == 3);
        CHECK(!first.ValidateUserRange(base + 3 * 4096, 1, false));
        const uint32_t one = physical(first, base), two = physical(first, base + 4096), three = physical(first, base + 8192);
        fill((uint8_t*)one, 4096, 0x41); fill((uint8_t*)two, 4096, 0x42); fill((uint8_t*)three, 4096, 0x43);
        CHECK(first.SetPermissions(handle, 0, 16 * 1024 * 1024, ProcessMemoryNone));
        CHECK(used() == initial + 4 && region(first, handle).residentPages == 3);
        CHECK(!first.ValidateUserRange(base, 1, false) && !first.ValidateUserRange(base + 8192, 1, false));
        CHECK(first.SetPermissions(handle, 0, 3 * 4096, ProcessMemoryReadWrite));
        CHECK(physical(first, base) == one && physical(first, base + 4096) == two && physical(first, base + 8192) == three);
        CHECK(all((uint8_t*)one, 4096, 0x41) && all((uint8_t*)two, 4096, 0x42) && all((uint8_t*)three, 4096, 0x43));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryRead));
        CHECK(first.CopyFromUser(output, base, 4096) && all(output, 4096, 0x41));
        CHECK(!first.CopyToUser(base, input, 1)); CHECK(all((uint8_t*)one, 4096, 0x41));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryNone));
        CHECK(!first.ValidateUserRange(base, 1, false)); fill(output, sizeof(output), 0x77);
        CHECK(!first.CopyFromUser(output, base, 4096) && all(output, sizeof(output), 0x77));
        CHECK(frames.isAllocated(one) && used() == initial + 4 && all((uint8_t*)one, 4096, 0x41));
        CHECK(first.SetPermissions(handle, 3 * 4096, 4096, ProcessMemoryNone)); CHECK(used() == initial + 4);
        CHECK(region(first, handle).residentPages == 3);
        CHECK(first.Discard(handle, 0, 4096)); CHECK(all((uint8_t*)one, 4096, 0));
        CHECK(!first.ValidateUserRange(base, 1, false) && frames.isAllocated(one));
        CHECK(all((uint8_t*)two, 4096, 0x42) && all((uint8_t*)three, 4096, 0x43));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(physical(first, base) == one && all((uint8_t*)one, 4096, 0)); fill((uint8_t*)one, 4096, 0x44);
        const uint32_t beforeFailure = used(), beforeHash = processHash(first);
        CHECK(!first.Discard(handle, 0, 4 * 4096)); CHECK(first.GetLastError() == ProcessMemoryNotMapped);
        CHECK(all((uint8_t*)one, 4096, 0x44) && all((uint8_t*)two, 4096, 0x42) && all((uint8_t*)three, 4096, 0x43));
        CHECK(used() == beforeFailure && processHash(first) == beforeHash); sameRegion(first, handle, info);
        const uint32_t badOffsets[] = {1, 4095, 16 * 1024 * 1024, 0xFFFFF000u, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(badOffsets) / sizeof(badOffsets[0]); ++i) {
            CHECK(!first.SetPermissions(handle, badOffsets[i], 4096, ProcessMemoryReadWrite));
            CHECK(!first.Decommit(handle, badOffsets[i], 4096)); CHECK(!first.Discard(handle, badOffsets[i], 4096));
        }
        const uint32_t badLengths[] = {0, 1, 4095, 4097, 0xFFFFF000u, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(badLengths) / sizeof(badLengths[0]); ++i) {
            CHECK(!first.SetPermissions(handle, 0, badLengths[i], ProcessMemoryRead));
            CHECK(!first.Decommit(handle, 0, badLengths[i])); CHECK(!first.Discard(handle, 0, badLengths[i]));
        }
        const uint32_t permissions[] = {2, 4, 5, 7, 0xFFFFFFFFu};
        for (uint32_t i = 0; i < sizeof(permissions) / sizeof(permissions[0]); ++i) {
            CHECK(!first.SetPermissions(handle, 0, 4096, permissions[i])); CHECK(first.GetLastError() == ProcessMemoryPermission);
        }
        CHECK(used() == beforeFailure && processHash(first) == beforeHash); sameRegion(first, handle, info);
        CHECK(first.Decommit(handle, 4096, 4096)); CHECK(!frames.isAllocated(two) && used() == initial + 3);
        CHECK(!first.ValidateUserRange(base + 4096, 1, false)); CHECK(region(first, handle).residentPages == 2);
        CHECK(all((uint8_t*)one, 4096, 0x44) && all((uint8_t*)three, 4096, 0x43));
        CHECK(first.Decommit(handle, 4096, 4096)); CHECK(used() == initial + 3); // Holes are idempotent.
        CHECK(first.SetPermissions(handle, 4096, 4096, ProcessMemoryRead));
        CHECK(used() == initial + 4 && all((uint8_t*)physical(first, base + 4096), 4096, 0));
        CHECK(!first.CopyToUser(base + 4096, input, 1));
        CHECK(first.Trim(handle, 2 * 4096)); CHECK(!frames.isAllocated(three) && used() == initial + 3);
        info = region(first, handle); CHECK(info.bytes == 8192 && info.residentPages == 2);
        CHECK(!first.SetPermissions(handle, 8192, 4096, ProcessMemoryReadWrite));
        CHECK(!first.Trim(handle, 0)); CHECK(!first.Trim(handle, 4095)); CHECK(!first.Trim(handle, 3 * 4096));
        sameRegion(first, handle, info); CHECK(!first.MapNewPage(UserBase + 4096, true));
        CHECK(!first.ProtectPage(UserBase, false)); CHECK(!first.UnmapPage(UserBase));
        CHECK(first.GetLastError() == ProcessMemorySealed);
        CHECK(first.Release(handle)); CHECK(used() == initial); invalidHandle(first, handle);
        uint32_t nextBase = 0, nextHandle = 0;
        CHECK(first.Reserve(4096, 4096, base, nextBase, nextHandle)); CHECK(nextBase == base && nextHandle > otherHandle);
        CHECK(first.SetPermissions(nextHandle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(first.SetPermissions(nextHandle, 0, 4096, ProcessMemoryNone));
        CHECK(first.Destroy()); CHECK(used() == baseline && templateHash() == shared);
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Seal());
        CHECK(first.Reserve(4096, 4096, base, nextBase, otherHandle)); CHECK(otherHandle > nextHandle);
        CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    void dynamicQuotaAndOwnership() {
        CHECK(first.Prepare(kernel, frames)); CHECK(first.MapNewPage(UserBase, true)); CHECK(first.Seal());
        uint32_t base = 0, handle = 0; CHECK(first.Reserve(256 * 4096, 4096, 0, base, handle));
        CHECK(first.SetPermissions(handle, 0, 255 * 4096, ProcessMemoryReadWrite));
        const uint32_t count = used(), hash = processHash(first);
        CHECK(region(first, handle).residentPages == 255);
        CHECK(!first.SetPermissions(handle, 255 * 4096, 4096, ProcessMemoryReadWrite)); CHECK(first.GetLastError() == ProcessMemoryLimit);
        CHECK(used() == count && processHash(first) == hash && region(first, handle).residentPages == 255);
        CHECK(first.SetPermissions(handle, 0, 255 * 4096, ProcessMemoryNone)); CHECK(used() == count);
        CHECK(!first.SetPermissions(handle, 255 * 4096, 4096, ProcessMemoryRead)); CHECK(first.GetLastError() == ProcessMemoryLimit);
        CHECK(first.Decommit(handle, 0, 4096)); CHECK(first.SetPermissions(handle, 255 * 4096, 4096, ProcessMemoryReadWrite));
        CHECK(region(first, handle).residentPages == 255);
        CHECK(first.Release(handle)); CHECK(first.Destroy()); CHECK(used() == baseline);
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Seal());
        uint32_t handles[ProcessAddressSpace::MaximumRegions], bases[ProcessAddressSpace::MaximumRegions];
        const uint32_t empty = used();
        for (uint32_t i = 0; i < ProcessAddressSpace::MaximumRegions; ++i) {
            CHECK(first.Reserve(4096, 4096, 0, bases[i], handles[i])); CHECK(used() == empty);
            for (uint32_t j = 0; j < i; ++j) CHECK(bases[i] != bases[j] && handles[i] > handles[j]);
        }
        reserveError(4096, 4096, 0, ProcessMemoryRegionLimit);
        invalidHandle(first, 0); invalidHandle(first, 0xFFFFFFFFu);
        CHECK(second.Prepare(kernel, frames)); CHECK(second.Seal());
        uint32_t otherBase = 0, otherHandle = 0; CHECK(second.Reserve(4096, 4096, bases[0], otherBase, otherHandle));
        CHECK(otherHandle > handles[ProcessAddressSpace::MaximumRegions - 1]); invalidHandle(second, handles[0]); invalidHandle(first, otherHandle);
        CHECK(first.Release(handles[0])); CHECK(first.Reserve(4096, 4096, bases[0], base, handle));
        CHECK(base == bases[0] && handle > otherHandle); // Failed Reserve consumed no handle.
        CHECK(handle == otherHandle + 1);
        CHECK(!first.SetPermissions(handles[1], 0, 8192, ProcessMemoryReadWrite)); // Cannot cross into its neighbor region.
        CHECK(!first.Decommit(handles[1], 0, 8192)); CHECK(!first.Discard(handles[1], 0, 8192));
        CHECK(first.Destroy()); CHECK(second.Destroy()); CHECK(used() == baseline);
    }
    void dynamicInvalidationsAndContext() {
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Seal());
        uint32_t base = 0, handle = 0; CHECK(first.Reserve(4 * 4096, 4096, 0, base, handle));
        gtos_process_memory_test_invalidations = 0;
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(gtos_process_memory_test_invalidations == 0); // Kernel CR3 is test-only mutation context.
        gtos_process_memory_test_cr3 = first.DirectoryAddress();
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryRead));
        CHECK(gtos_process_memory_test_invalidations == 1 && gtos_process_memory_test_invalidated[0] == base);
        gtos_process_memory_test_invalidations = 0; CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryNone));
        CHECK(gtos_process_memory_test_invalidations == 1 && gtos_process_memory_test_invalidated[0] == base);
        gtos_process_memory_test_invalidations = 0; CHECK(first.SetPermissions(handle, 4096, 4096, ProcessMemoryNone));
        CHECK(gtos_process_memory_test_invalidations == 0);
        CHECK(first.Discard(handle, 0, 4096)); CHECK(gtos_process_memory_test_invalidations == 0);
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryReadWrite)); CHECK(gtos_process_memory_test_invalidations == 1);
        gtos_process_memory_test_invalidations = 0; CHECK(first.Decommit(handle, 0, 4096));
        CHECK(gtos_process_memory_test_invalidations == 1 && gtos_process_memory_test_invalidated[0] == base);
        gtos_process_memory_test_invalidations = 0; CHECK(first.Decommit(handle, 0, 4096)); CHECK(gtos_process_memory_test_invalidations == 0);
        CHECK(first.SetPermissions(handle, 0, 3 * 4096, ProcessMemoryReadWrite)); CHECK(gtos_process_memory_test_invalidations == 3);
        for (uint32_t i = 0; i < 3; ++i) CHECK(gtos_process_memory_test_invalidated[i] == base + i * 4096);
        gtos_process_memory_test_invalidations = 0; CHECK(first.Trim(handle, 8192));
        CHECK(gtos_process_memory_test_invalidations == 1 && gtos_process_memory_test_invalidated[0] == base + 8192);
        const uint32_t count = used(), hash = processHash(first);
        gtos_process_memory_test_invalidations = 0;
        gtos_process_memory_test_bsp = false; CHECK(!first.SetPermissions(handle, 0, 4096, ProcessMemoryRead));
        CHECK(!first.Release(handle)); gtos_process_memory_test_bsp = true;
        gtos_process_memory_test_cr0 &= ~0x10000u; CHECK(!first.Decommit(handle, 0, 4096)); gtos_process_memory_test_cr0 |= 0x10000u;
        gtos_process_memory_test_cr4 = 1u << 5; CHECK(!first.Discard(handle, 0, 4096)); gtos_process_memory_test_cr4 = 0;
        CHECK(used() == count && processHash(first) == hash && !gtos_process_memory_test_invalidations);
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(second.Prepare(kernel, frames)); CHECK(second.Seal());
        gtos_process_memory_test_cr3 = second.DirectoryAddress();
        uint32_t outBase = 77, outHandle = 88; CHECK(!first.Reserve(4096, 4096, 0, outBase, outHandle));
        CHECK(outBase == 77 && outHandle == 88); CHECK(!first.SetPermissions(handle, 0, 4096, ProcessMemoryRead));
        CHECK(!first.Decommit(handle, 0, 4096)); CHECK(!first.Discard(handle, 0, 4096));
        CHECK(!first.Trim(handle, 4096)); CHECK(!first.Release(handle));
        ProcessMemoryRegionInfo out; fill((uint8_t*)&out, sizeof(out), 0x75); const ProcessMemoryRegionInfo sentinel = out;
        CHECK(!first.QueryRegion(handle, out)); CHECK(equal((uint8_t*)&out, (uint8_t*)&sentinel, sizeof(out)));
        CHECK(used() == count + 1 && processHash(first) == hash && !gtos_process_memory_test_invalidations);
        gtos_process_memory_test_cr3 = first.DirectoryAddress(); CHECK(first.Release(handle));
        CHECK(gtos_process_memory_test_invalidations == 2);
        CHECK(gtos_process_memory_test_invalidated[0] == base && gtos_process_memory_test_invalidated[1] == base + 4096);
        CHECK(!first.Destroy()); CHECK(first.GetLastError() == ProcessMemoryActive);
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(first.Destroy()); CHECK(second.Destroy()); CHECK(used() == baseline);
    }
    void dynamicAllocationRollback(bool existingTable, bool twoNewTables = false) {
        CHECK(first.Prepare(kernel, frames)); CHECK(first.Seal());
        uint32_t base = 0, handle = 0;
        CHECK(first.Reserve(3 * 4096, 4096, (existingTable || twoNewTables) ? 0x813FF000u : 0x81000000u, base, handle));
        uint32_t saved = 0;
        if (existingTable) {
            CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryRead)); saved = physical(first, base);
            fill((uint8_t*)saved, 4096, 0x6B);
        }
        const ProcessMemoryRegionInfo before = region(first, handle);
        const uint32_t original = used(), hash = processHash(first);
        uint32_t held = 0, frame = 0;
        while (frames.allocate(frame)) retained[held++] = frame;
        const uint32_t required = existingTable ? 3 : (twoNewTables ? 5 : 4); // Data plus actual missing table frames.
        gtos_process_memory_test_cr3 = first.DirectoryAddress();
        for (uint32_t freePages = 0; freePages < required; ++freePages) {
            const uint32_t count = used(); gtos_process_memory_test_invalidations = 0;
            CHECK(!first.SetPermissions(handle, 0, 3 * 4096, ProcessMemoryReadWrite));
            CHECK(first.GetLastError() == ProcessMemoryNoMemory);
            CHECK(used() == count && processHash(first) == hash && !gtos_process_memory_test_invalidations);
            CHECK(frames.getStatistics().freeFrames == freePages); sameRegion(first, handle, before);
            if (existingTable) {
                CHECK(physical(first, base) == saved && all((uint8_t*)saved, 4096, 0x6B));
                CHECK(first.ValidateUserRange(base, 4096, false) && !first.ValidateUserRange(base, 1, true));
            }
            CHECK(held && frames.free(retained[--held]));
        }
        CHECK(first.SetPermissions(handle, 0, 3 * 4096, ProcessMemoryReadWrite));
        CHECK(region(first, handle).residentPages == 3 && !frames.getStatistics().freeFrames);
        CHECK(gtos_process_memory_test_invalidations == 3);
        for (uint32_t i = 0; i < 3; ++i) CHECK(gtos_process_memory_test_invalidated[i] == base + i * 4096);
        const uint32_t start = existingTable ? 1 : 0;
        for (uint32_t i = start; i < 3; ++i) CHECK(all((uint8_t*)physical(first, base + i * 4096), 4096, 0));
        if (existingTable) CHECK(all((uint8_t*)saved, 4096, 0x6B));
        CHECK(first.Release(handle));
        while (held) CHECK(frames.free(retained[--held]));
        CHECK(used() == original - (existingTable ? 2u : 0u));
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(first.Destroy()); CHECK(used() == baseline);
    }
    // The ProtectRange witnesses are counters, not flags, so every case resets them: a case that inherits another case's
    // calls can report a number without being able to say which call produced it.
    void seamReset() {
        gtos_process_memory_test_fail_protect_after = 0;
        gtos_process_memory_test_protect_calls = 0;
        gtos_process_memory_test_protect_refusals = 0;
        gtos_process_memory_test_protect_pages_at_refusal = 0;
    }
    // Walks the loader's own Create sequence against the real address space: prepare, map code, map data, copy, protect.
    // NativeRuntime::Create is what CallProtocol reaches in production and this is its fifth stage; the sequence is
    // reproduced here at the layer this binary can link, and the rollback it uses is the same Destroy.
    void protectRangeSeam() {
        seamReset();
        CHECK(first.Prepare(kernel, frames));
        const uint32_t prepared = used();
        CHECK(first.MapNewPage(UserBase, true));
        CHECK(first.MapNewPage(UserBase + 4096, true));
        CHECK(used() >= prepared + 2); // Both data frames, plus whatever page table the mapping needed.
        fill(input, 4096, 0x5A);
        CHECK(first.CopyToUser(UserBase, input, 4096));
        CHECK(first.CopyToUser(UserBase + 4096, input, 4096));
        const uint32_t codeFrame = physical(first, UserBase), dataFrame = physical(first, UserBase + 4096);

        // Default path: no injection. The protection the loader asks for goes through the real loop, one consultation,
        // no refusal, the page keeps its frame and its content and loses only its writability.
        CHECK(gtos_process_memory_test_protect_calls == 0 && gtos_process_memory_test_protect_refusals == 0);
        CHECK(first.ProtectPage(UserBase, false));
        CHECK(gtos_process_memory_test_protect_calls == 1 && gtos_process_memory_test_protect_refusals == 0);
        CHECK((entry(first, UserBase) & 2u) == 0u);
        CHECK(physical(first, UserBase) == codeFrame);
        CHECK(first.CopyFromUser(output, UserBase, 4096) && all(output, 4096, 0x5A));
        CHECK((entry(first, UserBase + 4096) & 2u) == 2u); // The data page is not in the protected range.

        // Boundary and illegal ranges: the seam is consulted and refuses nothing, and the real validation rejects with
        // the real reason. A refusal counted here would mean the seam, not the range check, ended the call.
        struct BadRange { uint32_t address, bytes; ProcessMemoryError error; };
        // The arena itself is a legal range: from UserBase, the largest length still inside it ends exactly at the exclusive
        // limit, so that call is rejected because its pages are missing and not because its arithmetic overflowed. One byte
        // more is the range error. Both are needed; only the second one tests the limit.
        const BadRange bad[] = {
            {UserBase, 0, ProcessMemoryBadRange},
            {UserBase, 4095, ProcessMemoryBadRange},
            {UserBase + 1, 4096, ProcessMemoryBadRange},
            {UserBase, 0x80000001u, ProcessMemoryBadRange},
            {UserBase, 0x80000000u, ProcessMemoryNotMapped},
            {UserBase + 8192, 4096, ProcessMemoryNotMapped},
        };
        for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            const uint32_t count = used(), hash = processHash(first);
            const uint32_t calls = gtos_process_memory_test_protect_calls;
            CHECK(!first.ProtectRange(bad[i].address, bad[i].bytes, true));
            CHECK(first.GetLastError() == bad[i].error);
            CHECK(gtos_process_memory_test_protect_calls == calls + 1);
            CHECK(gtos_process_memory_test_protect_refusals == 0);
            CHECK(used() == count && processHash(first) == hash);
        }

        // A live peer object, so the failed attempt can be shown not to reach it. The footprint `first` already occupies is
        // measured here, because the reclamation checks below have to name the count Destroy must return to rather than the
        // count the peer alone would leave: `first` was prepared before the peer existed.
        const uint32_t withFirstOnly = used();
        CHECK(second.Prepare(kernel, frames));
        CHECK(second.MapNewPage(UserBase, true));
        const uint32_t firstFootprint = withFirstOnly - baseline;
        const uint32_t peerUsed = used(), peerHash = processHash(second);

        // The fifth stage refused after both frames and both page tables were already taken. The threshold is the page
        // count that stage really sees, so a refusal recorded at any other count would be a different, earlier failure.
        const uint32_t beforeRefusal = used(), hashBeforeRefusal = processHash(first);
        const uint32_t callsBefore = gtos_process_memory_test_protect_calls;
        gtos_process_memory_test_protect_refusals = 0;
        gtos_process_memory_test_protect_pages_at_refusal = 0;
        gtos_process_memory_test_fail_protect_after = 2;
        const bool late = first.ProtectPage(UserBase, false);
        gtos_process_memory_test_fail_protect_after = 0;
        CHECK(!late);
        CHECK(first.GetLastError() == ProcessMemoryNoMemory);
        CHECK(gtos_process_memory_test_protect_calls - callsBefore == 1);
        CHECK(gtos_process_memory_test_protect_refusals == 1);
        CHECK(gtos_process_memory_test_protect_pages_at_refusal == 2);
        // Nothing was published or partially applied: same frames, same writability, same content, same count.
        CHECK(used() == beforeRefusal && processHash(first) == hashBeforeRefusal);
        CHECK(physical(first, UserBase) == codeFrame && physical(first, UserBase + 4096) == dataFrame);
        CHECK((entry(first, UserBase) & 2u) == 0u); // Still read-only from the earlier successful call.
        CHECK(first.CopyFromUser(output, UserBase + 4096, 4096) && all(output, 4096, 0x5A));
        CHECK(used() == peerUsed && processHash(second) == peerHash);

        // The loader's rollback for a refused Create is Destroy on the same object. Every frame it took returns to the
        // allocator, and the peer keeps its own frames and its own layout.
        CHECK(first.Destroy());
        CHECK(used() + firstFootprint == peerUsed && processHash(second) == peerHash);

        // The same instance retried with the seam withdrawn: the fifth stage succeeds and the object is fully reclaimed.
        seamReset();
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.MapNewPage(UserBase, true) && first.MapNewPage(UserBase + 4096, true));
        CHECK(first.CopyToUser(UserBase, input, 4096));
        CHECK(first.ProtectPage(UserBase, false));
        CHECK(gtos_process_memory_test_protect_calls == 1 && gtos_process_memory_test_protect_refusals == 0);
        CHECK((entry(first, UserBase) & 2u) == 0u);
        CHECK(first.Destroy());
        CHECK(used() + firstFootprint == peerUsed && processHash(second) == peerHash);
        CHECK(second.Destroy());
        CHECK(used() == baseline);

        // A whole-range refusal: three pages already owned, the seam refuses the range at page count three, and no page of
        // the range was modified. This is the shape CreateElf uses for a read-only segment.
        seamReset();
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.MapNewPage(UserBase, true));
        CHECK(first.MapNewPage(UserBase + 4096, true));
        CHECK(first.MapNewPage(UserBase + 8192, true));
        const uint32_t rangeHash = processHash(first), rangeUsed = used();
        gtos_process_memory_test_fail_protect_after = 3;
        CHECK(!first.ProtectRange(UserBase, 3 * 4096, false));
        gtos_process_memory_test_fail_protect_after = 0;
        CHECK(first.GetLastError() == ProcessMemoryNoMemory);
        CHECK(gtos_process_memory_test_protect_refusals == 1);
        CHECK(gtos_process_memory_test_protect_pages_at_refusal == 3);
        CHECK(used() == rangeUsed && processHash(first) == rangeHash);
        for (uint32_t i = 0; i < 3; ++i) CHECK((entry(first, UserBase + i * 4096) & 2u) == 2u);
        CHECK(first.Destroy());
        CHECK(used() == baseline);

        // Stability: the armed/unarmed cycle repeated on the same instance must end at the same count every time and leave
        // nothing behind. A seam that is not restored is visible here as a rising count.
        for (uint32_t round = 0; round < 8; ++round) {
            seamReset();
            CHECK(first.Prepare(kernel, frames));
            CHECK(first.MapNewPage(UserBase, true) && first.MapNewPage(UserBase + 4096, true));
            gtos_process_memory_test_fail_protect_after = 2;
            CHECK(!first.ProtectPage(UserBase, false));
            CHECK(first.GetLastError() == ProcessMemoryNoMemory);
            CHECK(gtos_process_memory_test_protect_refusals == 1);
            gtos_process_memory_test_fail_protect_after = 0;
            CHECK(first.ProtectPage(UserBase, false));
            CHECK(gtos_process_memory_test_protect_refusals == 1);
            CHECK(first.Destroy());
            CHECK(used() == baseline);
        }
        seamReset();
        CHECK(gtos_process_memory_test_protect_calls == 0 && gtos_process_memory_test_protect_refusals == 0
              && gtos_process_memory_test_protect_pages_at_refusal == 0);
    }
    void indexedOwnership() {
        const uint32_t alias = UserBase + 32 * 1024 * 1024;
        CHECK(first.Prepare(kernel, frames));
        CHECK(second.Prepare(kernel, frames));
        CHECK(first.MapNewPage(UserBase, true));
        CHECK(first.MapNewPage(alias, true));
        CHECK(second.MapNewPage(alias, true));
        fill(input, 4096, 0x35);
        CHECK(first.CopyToUser(UserBase, input, 4096));
        fill(input, 4096, 0x63);
        CHECK(first.CopyToUser(alias, input, 4096));
        fill(input, 4096, 0x9A);
        CHECK(second.CopyToUser(alias, input, 4096));
        CHECK(first.CopyFromUser(output, UserBase, 4096) && all(output, 4096, 0x35));
        CHECK(first.ProtectPage(alias, false));
        CHECK(!first.CopyToUser(alias, input, 4096));
        CHECK(first.GetLastError() == ProcessMemoryPermission);
        CHECK(first.UnmapPage(UserBase));
        CHECK(first.CopyFromUser(output, alias, 4096) && all(output, 4096, 0x63));
        CHECK(!first.MapNewPage(alias, true));
        CHECK(first.GetLastError() == ProcessMemoryConflict);
        CHECK(second.CopyFromUser(output, alias, 4096) && all(output, 4096, 0x9A));
        CHECK(first.Destroy()); CHECK(second.Destroy()); CHECK(used() == baseline);
    }
    void contentCopies() {
        const uint32_t capacity = ProcessAddressSpace::MaximumCapacityPages + 1;
        CHECK(first.PrepareContent(kernel, frames, capacity, ProcessAddressSpace::DynamicLimit));
        CHECK(first.MapNewRange(UserBase, 8192, true));
        CHECK(second.Prepare(kernel, frames));
        CHECK(second.MapNewPage(UserBase, true));
        CHECK(first.Seal());
        fill(input, 64, 0x42);
        CHECK(first.CopyToUser(UserBase + 4096 - 32, input, 64));
        CHECK(second.CopyToUser(UserBase, input, 32)); // Ordinary scratch index can be reused.
        CHECK(first.CopyFromUser(output, UserBase + 4096 - 32, 64) && all(output, 64, 0x42));

        uint32_t& stray = table(first, UserBase)[2];
        const uint32_t firstLeaf = entry(first, UserBase);
        const uint32_t count = used();
        stray = 0x20; // A nonpresent nonzero entry is still unowned.
        CHECK(!first.SetDataReadOnly(UserBase, 4096));
        CHECK(entry(first, UserBase) == firstLeaf && used() == count);
        stray = firstLeaf;
        entry(first, UserBase) = 0; // An extra leaf cannot replace an absent owned one.
        CHECK(!first.SetDataReadOnly(UserBase, 4096));
        CHECK(entry(first, UserBase) == 0 && used() == count);
        stray = 0;
        entry(first, UserBase) = firstLeaf;

        uint32_t& leaf = entry(first, UserBase + 4096);
        const uint32_t savedLeaf = leaf;
        leaf = (leaf & 4095) | physical(second, UserBase);
        fill(output, 64, 0x85);
        CHECK(!first.CopyFromUser(output, UserBase + 4096 - 32, 64));
        CHECK(all(output, 64, 0x85));
        leaf = savedLeaf;
        CHECK(first.CopyFromUser(output, UserBase + 4096 - 32, 64));
        uint32_t& pde = directory(first)[UserBase >> 22];
        const uint32_t savedPde = pde;
        pde &= ~4u;
        CHECK(!first.ValidateUserRange(UserBase, 1, false));
        pde = savedPde | 0x20;
        leaf |= 0x60;
        CHECK(first.ValidateUserRange(UserBase + 4096, 1, true));
        CHECK(first.SetDataReadOnly(UserBase + 4096, 4096));
        CHECK(!first.CopyToUser(UserBase + 4096 - 32, output, 64));
        CHECK(first.CopyFromUser(output, UserBase + 4096 - 32, 64) && all(output, 64, 0x42));

        uint32_t base, handle;
        const uint32_t beforeReservation = used();
        CHECK(first.Reserve(8192, 4096, 0, base, handle));
        CHECK(used() == beforeReservation);
        CHECK(first.CopyFromUser(output, UserBase, 64));
        CHECK(!first.ValidateUserRange(base, 1, false));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryNone));
        CHECK(used() == beforeReservation && !first.ValidateUserRange(base, 1, false));
        CHECK(first.SetPermissions(handle, 0, 8192, ProcessMemoryReadWrite));
        CHECK(first.CopyToUser(base + 4096, input, 32));
        const uint32_t resident = used();
        CHECK(first.SetPermissions(handle, 4096, 4096, ProcessMemoryNone));
        CHECK(used() == resident && !first.ValidateUserRange(base + 4096, 1, false));
        CHECK(first.SetPermissions(handle, 4096, 4096, ProcessMemoryRead));
        CHECK(used() == resident && !first.CopyToUser(base + 4096, input, 32));
        CHECK(first.CopyFromUser(output, base + 4096, 32) && all(output, 32, 0x42));
        CHECK(first.Decommit(handle, 0, 4096)); // Swaps the surviving ownership record.
        CHECK(!first.ValidateUserRange(base, 1, false));
        CHECK(first.CopyFromUser(output, base + 4096, 32) && all(output, 32, 0x42));
        CHECK(first.Release(handle));
        CHECK(!first.ValidateUserRange(base + 4096, 1, false));
        CHECK(first.Destroy());
        CHECK(second.Destroy());
        CHECK(used() == baseline);
    }
    void contentRegionCapacity() {
        const uint32_t limit = ProcessAddressSpace::MaximumContentRegions;
        CHECK(first.PrepareContent(kernel, frames, ProcessAddressSpace::MaximumPages,
                                   ProcessAddressSpace::DynamicLimit));
        CHECK(first.CapacityRegions() == limit && first.Seal());
        const uint32_t empty = used();
        uint32_t handles[ProcessAddressSpace::MaximumContentRegions];
        uint32_t base = 0;
        for (uint32_t i = 0; i < limit; ++i) {
            const uint32_t expected = ProcessAddressSpace::DynamicBase + i * 4096;
            const uint32_t hint = i + 1 == limit ? 0 : expected;
            CHECK(first.Reserve(4096, 4096, hint, base, handles[i]));
            CHECK(base == expected);
        }
        CHECK(used() == empty);
        const uint32_t last = base;
        uint32_t extra = 0;
        CHECK(!first.Reserve(4096, 4096, 0, base, extra));
        CHECK(first.GetLastError() == ProcessMemoryRegionLimit && base == last && extra == 0);
        CHECK(first.SetPermissions(handles[limit - 1], 0, 4096, ProcessMemoryReadWrite));
        fill(input, 32, 0x96);
        CHECK(first.CopyToUser(last, input, 32));
        CHECK(first.CopyFromUser(output, last, 32) && all(output, 32, 0x96));
        CHECK(first.Release(handles[0]));
        CHECK(first.Reserve(4096, 4096, 0, base, extra));
        CHECK(base == ProcessAddressSpace::DynamicBase && extra == handles[limit - 1] + 1);
        CHECK(first.Destroy() && used() == baseline);
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.CapacityRegions() == ProcessAddressSpace::MaximumRegions && first.Seal());
        CHECK(first.Reserve(4096, 4096, 0, base, extra) && base == ProcessAddressSpace::DynamicBase);
        CHECK(first.Destroy() && used() == baseline);
    }
    void contentIndexSizing() {
        // The capacity places both index arrays. The number of live pages can
        // be much smaller and must remain indexed across removal and reseal.
        CHECK(first.PrepareContent(kernel, frames, 16385, ProcessAddressSpace::DynamicLimit));
        const uint32_t length = 96 * 4096;
        CHECK(first.MapNewRange(UserBase, length, true));
        CHECK(first.UnmapPage(UserBase + 4096));
        CHECK(first.MapNewPage(UserBase + 4096, true));
        CHECK(first.Seal());
        fill(input, 128, 0x63);
        CHECK(first.CopyToUser(UserBase + length - 4096 - 64, input, 128));
        CHECK(first.CopyFromUser(output, UserBase + length - 4096 - 64, 128));
        CHECK(all(output, 128, 0x63));
        uint32_t base, handle;
        CHECK(first.Reserve(2 * 4096, 4096, 0, base, handle));
        CHECK(first.SetPermissions(handle, 0, 2 * 4096, ProcessMemoryReadWrite));
        CHECK(first.CopyToUser(base + 4096 - 64, input, 128));
        CHECK(first.Decommit(handle, 0, 4096));
        CHECK(!first.ValidateUserRange(base, 1, false));
        CHECK(first.CopyFromUser(output, base + 4096, 64) && all(output, 64, 0x63));
        CHECK(first.Release(handle));
        CHECK(first.Destroy());
        CHECK(used() == baseline);
    }
    void contentLargeCommit() {
        const uint32_t pages = 1300, bytes = pages * 4096;
        const uint32_t hint = 0x813FF000u; // Commit crosses two page-table boundaries.
        uint32_t base = 0, handle = 0;
        CHECK(first.Prepare(kernel, frames, 2048) && first.Seal());
        CHECK(first.Reserve(bytes, 4096, hint, base, handle));
        const uint32_t ordinary = used();
        CHECK(!first.SetPermissions(handle, 0, bytes, ProcessMemoryReadWrite));
        CHECK(first.GetLastError() == ProcessMemoryLimit && used() == ordinary);
        CHECK(first.Destroy() && used() == baseline);

        CHECK(first.PrepareContent(kernel, frames, 2048, ProcessAddressSpace::DynamicLimit));
        CHECK(first.Seal() && first.Reserve(bytes, 4096, hint, base, handle));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryRead));
        const uint32_t saved = physical(first, base);
        fill((uint8_t*)saved, 4096, 0x6B);
        const uint32_t hash = processHash(first);
        uint32_t held = 0, frame = 0;
        while (frames.getStatistics().freeFrames > 1040 && frames.allocate(frame))
            retained[held++] = frame;
        const uint32_t exhausted = used();
        gtos_process_memory_test_cr3 = first.DirectoryAddress();
        gtos_process_memory_test_invalidations = 0;
        CHECK(!first.SetPermissions(handle, 0, bytes, ProcessMemoryReadWrite));
        CHECK(first.GetLastError() == ProcessMemoryNoMemory);
        CHECK(processHash(first) == hash && used() == exhausted);
        CHECK(!gtos_process_memory_test_invalidations);
        CHECK(region(first, handle).residentPages == 1);
        CHECK(physical(first, base) == saved && all((uint8_t*)saved, 4096, 0x6B));
        CHECK(!first.ValidateUserRange(base, 1, true));
        while (held) CHECK(frames.free(retained[--held]));

        CHECK(first.SetPermissions(handle, 0, bytes, ProcessMemoryReadWrite));
        CHECK(region(first, handle).residentPages == pages);
        CHECK(gtos_process_memory_test_invalidations == pages);
        CHECK(physical(first, base) == saved && all((uint8_t*)saved, 4096, 0x6B));
        CHECK(all((uint8_t*)physical(first, base + bytes - 4096), 4096, 0));
        CHECK(first.ValidateUserRange(base, bytes, true));
        CHECK(first.Release(handle));
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(first.Destroy() && used() == baseline);
    }
    void partialTableRelease() {
        const uint32_t seam = ProcessAddressSpace::DynamicBase + (1u << 22);
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.Seal());
        uint32_t base, handle, neighbor, neighborHandle;
        CHECK(first.Reserve(4 * 4096, 4096, seam - 8192, base, handle));
        CHECK(first.Reserve(4096, 4096, seam + 8192, neighbor, neighborHandle));
        CHECK(first.SetPermissions(handle, 0, 4 * 4096, ProcessMemoryReadWrite));
        CHECK(first.SetPermissions(neighborHandle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(first.SetPermissions(handle, 0, 4096, ProcessMemoryNone));
        CHECK(first.SetPermissions(neighborHandle, 0, 4096, ProcessMemoryNone));
        const uint32_t lowTable = (uint32_t)table(first, base);
        const uint32_t highTable = (uint32_t)table(first, seam);
        const uint32_t committed = used();
        gtos_process_memory_test_cr3 = first.DirectoryAddress();
        gtos_process_memory_test_invalidations = 0;
        CHECK(first.Decommit(handle, 4096, 8192));
        CHECK(gtos_process_memory_test_invalidations == 2);
        CHECK(used() == committed - 2 && frames.isAllocated(lowTable) && frames.isAllocated(highTable));
        CHECK(first.Release(handle));
        CHECK(gtos_process_memory_test_invalidations == 4);
        CHECK(used() == committed - 5 && !frames.isAllocated(lowTable) && frames.isAllocated(highTable));
        CHECK(first.SetPermissions(neighborHandle, 0, 4096, ProcessMemoryReadWrite));
        CHECK(used() == committed - 5 && first.ValidateUserRange(neighbor, 4096, true));
        CHECK(first.Release(neighborHandle));
        CHECK(gtos_process_memory_test_invalidations == 6);
        CHECK(!frames.isAllocated(highTable));
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
        CHECK(first.Destroy());
        CHECK(used() == baseline);
    }
    void dynamicVirtualMemory() {
        dynamicRegionsAndPermissions(); dynamicQuotaAndOwnership(); dynamicInvalidationsAndContext();
        dynamicAllocationRollback(false); dynamicAllocationRollback(true); dynamicAllocationRollback(false, true);
        protectRangeSeam();
        indexedOwnership();
        contentCopies(); contentRegionCapacity(); contentIndexSizing(); contentLargeCommit(); partialTableRelease();
    }
    uint64_t* paeDirectory(uint32_t root, uint32_t address) {
        return (uint64_t*)(uint32_t)(((uint64_t*)root)[address >> 30] & ~4095ULL);
    }
    void destroyScrubsOwnedRuns() {
        CHECK(first.Prepare(kernel, frames));
        CHECK(first.MapNewRange(UserBase, 8192, true));
        uint32_t held = 0;
        CHECK(frames.allocate(held));
        CHECK(first.MapNewPage(UserBase + 8192, true));
        const uint32_t owned[] = {physical(first, UserBase), physical(first, UserBase + 4096),
                                  physical(first, UserBase + 8192)};
        CHECK(owned[1] == owned[0] + 4096);
        CHECK(held == owned[1] + 4096 && owned[2] == held + 4096);
        for (uint32_t address : owned) fill((uint8_t*)address, 4096, 0xA7);
        fill((uint8_t*)held, 4096, 0x53);
        CHECK(first.Seal() && first.Destroy());
        for (uint32_t address : owned)
            CHECK(frames.isFree(address) && all((uint8_t*)address, 4096, 0));
        CHECK(used() == baseline + 1 && all((uint8_t*)held, 4096, 0x53));
        CHECK(frames.free(held) && used() == baseline);
    }
    void paeAddressSpaces() {
        PagingConfig config = {(uint32_t)&__executable_start, ((uint32_t)&_end + 4095) & ~4095u,
            (uint32_t)&__executable_start, (uint32_t)&__executable_start + 4096, &info, 0, 0};
        uint32_t mapped = 0;
        const PagingDeviceRange bar(0xC000000000ULL, 16384, &mapped);
        config.devices = &bar;
        config.deviceCount = 1;
        CHECK(paeKernel.prepareIdentity(frames, config, true));
        CHECK(mapped >= ProcessAddressSpace::UserLimit);
        if (!mapped) return;
        CHECK(paeKernel.enable() && paeKernel.sealForSharedProcessors());
        const uint32_t sharedRoot = paeKernel.getStatistics().directoryAddress;
        const uint32_t before = used();
        gtos_process_memory_test_cr3 = sharedRoot;
        CHECK(!first.Prepare(paeKernel, frames) && used() == before);
        gtos_process_memory_test_cr4 = 1U << 5;
        // Exercise the exact QEMU access-bit update on the live kernel root.
        uint64_t* sharedPdpt = (uint64_t*)sharedRoot;
        sharedPdpt[0] |= 0x20;
        uint64_t& mmioDirectory = paeDirectory(sharedRoot, mapped)[(mapped >> 21) & 511];
        const uint64_t mmioPde = mmioDirectory;
        uint64_t* leaves = (uint64_t*)(uint32_t)(mmioPde & ~4095ULL);
        mmioDirectory = 0;
        CHECK(!first.Prepare(paeKernel, frames) && first.GetLastError() == ProcessMemoryBadTemplate);
        CHECK(!first.DirectoryAddress() && used() == before);
        mmioDirectory = mmioPde;
        uint32_t spare = 0;
        CHECK(frames.allocate(spare));
        if (spare) {
            fill((uint8_t*)spare, 4096, 0);
            mmioDirectory = spare | 3;
            CHECK(!first.Prepare(paeKernel, frames) && first.GetLastError() == ProcessMemoryBadTemplate);
            CHECK(!first.DirectoryAddress() && used() == before + 1);
            mmioDirectory = mmioPde | 0x20;
            CHECK(frames.free(spare));
        }
        const uint32_t ti = (mapped >> 12) & 511;
        const uint64_t original = leaves[ti];
        CHECK(original == (bar.address | 0x1b));
        const uint64_t bad[] = {0, original & 0xFFFFFFFFu, original ^ (1ULL << 32),
                                original | 4, original & ~0x10ULL, original | (1ULL << 63)};
        for (uint64_t changed : bad) {
            leaves[ti] = changed;
            CHECK(!first.Prepare(paeKernel, frames) && first.GetLastError() == ProcessMemoryBadTemplate);
            CHECK(!first.DirectoryAddress() && used() == before);
        }
        leaves[ti] = original | 0x60;
        CHECK(ti + 4 < 512 && !leaves[ti + 4]);
        leaves[ti + 4] = original;
        CHECK(!first.Prepare(paeKernel, frames) && first.GetLastError() == ProcessMemoryBadTemplate);
        leaves[ti + 4] = 0;

        // Fail after acquiring a root and part of its directory set.
        uint32_t held = 0;
        while (frames.getStatistics().freeFrames > 3 && held < 4096 && frames.allocate(retained[held]))
            ++held;
        CHECK(frames.getStatistics().freeFrames == 3);
        CHECK(!first.Prepare(paeKernel, frames) && first.GetLastError() == ProcessMemoryNoMemory);
        CHECK(!first.DirectoryAddress() && frames.getStatistics().freeFrames == 3);
        while (held) frames.free(retained[--held]);
        CHECK(used() == before);

        CHECK(first.Prepare(paeKernel, frames) && used() == before + 5);
        const uint32_t root = first.DirectoryAddress();
        if (!root) return;
        const uint32_t boundary = ProcessAddressSpace::DynamicBase;
        CHECK(paeDirectory(root, boundary) != paeDirectory(sharedRoot, boundary));
        CHECK(first.MapNewRange(boundary - 4096, 8192, true));
        // Large unaligned chunks on both sides of the PAE boundary, with
        // one tail byte before the boundary and two after it.
        for (uint32_t i = 1; i <= 1031; ++i) input[i] = (uint8_t)(i * 37 + 13);
        fill(output, 1033, 0xCC);
        CHECK(first.CopyToUser(boundary - 517, input + 1, 1031));
        CHECK(first.CopyFromUser(output + 1, boundary - 517, 1031) &&
              equal(input + 1, output + 1, 1031));
        CHECK(output[0] == 0xCC && output[1032] == 0xCC);
        fill(input, 32, 0x79);
        CHECK(first.CopyToUser(boundary - 16, input, 32));
        CHECK(first.CopyFromUser(output, boundary - 16, 32) && equal(input, output, 32));
        CHECK(!first.CopyFromUser(paeDirectory(root, boundary), boundary - 16, 32));
        CHECK(!first.CopyFromUser(paeDirectory(sharedRoot, boundary), boundary - 16, 32));

        uint64_t* userDirectory = paeDirectory(root, boundary);
        const uint64_t pde = userDirectory[0];
        CHECK(pde & 1);
        if (!(pde & 1)) return;
        uint64_t* userTable = (uint64_t*)(uint32_t)(pde & ~4095ULL);
        uint64_t* sharedDirectory = paeDirectory(sharedRoot, (uint32_t)input);
        const uint32_t sharedIndex = ((uint32_t)input >> 21) & 511;
        const uint64_t sharedPde = sharedDirectory[sharedIndex];
        CHECK(sharedPde & 1);
        uint64_t* processShared = paeDirectory(root, (uint32_t)input);
        const uint64_t processPde = processShared[sharedIndex];
        processShared[sharedIndex] = sharedPde ^ 0x20;
        CHECK(first.ValidateUserRange(boundary, 1, false));
        processShared[sharedIndex] = sharedPde ^ (1ULL << 32);
        CHECK(!first.ValidateUserRange(boundary, 1, false)
              && first.GetLastError() == ProcessMemoryBadMapping);
        processShared[sharedIndex] = processPde;
        uint32_t empty = 0;
        while (empty < 512 && sharedDirectory[empty]) ++empty;
        CHECK(empty < 512);
        if (empty < 512) {
            const uint64_t absent = processShared[empty];
            processShared[empty] = 0x20;
            CHECK(!first.ValidateUserRange(boundary, 1, false)
                  && first.GetLastError() == ProcessMemoryBadMapping);
            processShared[empty] = absent;
        }
        CHECK(!first.CopyFromUser(userTable, boundary - 16, 32)
              && first.GetLastError() == ProcessMemoryBadAlias);
        CHECK(!first.CopyFromUser((void*)(uint32_t)(sharedPde & ~4095ULL), boundary - 16, 32)
              && first.GetLastError() == ProcessMemoryBadAlias);
        const uint64_t pte = userTable[0];
        userTable[0] = pte | (1ULL << 32);
        CHECK(!first.ValidateUserRange(boundary, 1, false));
        userTable[0] = pte;
        userDirectory[0] = pde | 0x20;
        CHECK(first.ValidateUserRange(boundary, 1, false));
        userDirectory[0] = pde | (1ULL << 32);
        CHECK(!first.ValidateUserRange(boundary, 1, false)
              && first.GetLastError() == ProcessMemoryBadMapping);
        userDirectory[0] = pde;
        uint64_t* pdpt = (uint64_t*)root;
        const uint64_t pdpte = pdpt[2];
        pdpt[2] = pdpte | 0x20;
        CHECK(first.ValidateUserRange(boundary, 1, false));
        pdpt[2] = pdpte | 0x40;
        CHECK(!first.ValidateUserRange(boundary, 1, false));
        pdpt[2] = pdpte | (1ULL << 32);
        CHECK(!first.ValidateUserRange(boundary, 1, false));
        pdpt[2] = pdpte;
        CHECK(first.ProtectPage(boundary, false) && !first.ValidateUserRange(boundary, 1, true));
        CHECK(first.Seal());
        gtos_process_memory_test_cr3 = root;
        CHECK(!first.Destroy() && first.GetLastError() == ProcessMemoryActive);
        uint32_t base = 0, handle = 0;
        const uint32_t hint = boundary + 0x1FF000;
        CHECK(first.Reserve(8192, 4096, hint, base, handle) && base == hint);
        CHECK(first.SetPermissions(handle, 0, 8192, ProcessMemoryReadWrite));
        CHECK(first.CopyToUser(base + 4092, input, 8));
        CHECK(first.CopyFromUser(output, base + 4092, 8) && equal(input, output, 8));
        // The reservation crosses a PAE table boundary. Clear precisely the
        // first page and preserve the adjacent page and the mapping itself.
        fill(input, 4096, 0xA7);
        CHECK(first.CopyToUser(base, input, 4096) && first.CopyToUser(base + 4096, input, 4096));
        CHECK(first.Discard(handle, 0, 4096));
        CHECK(first.CopyFromUser(output, base, 4096) && all(output, 4096, 0));
        CHECK(first.CopyFromUser(output, base + 4096, 4096) && all(output, 4096, 0xA7));
        CHECK(first.Release(handle));
        CHECK(!first.ValidateUserRange(base, 1, false));
        gtos_process_memory_test_cr3 = sharedRoot;
        CHECK(first.Destroy() && used() == before);
        gtos_process_memory_test_cr4 = 0;
        gtos_process_memory_test_cr3 = kernel.getStatistics().directoryAddress;
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
    dynamicVirtualMemory(); destroyScrubsOwnedRuns(); paeAddressSpaces();
    print("Process memory tests: "); number(checks); print(" checks, "); number(failures); print(" failures\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n xorl %ebp,%ebp\n andl $-16,%esp\n call processMemoryTests\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
