#include <memory/process_address_space.h>
#include <memory/criticalsection.h>
using namespace gtos::memory;
namespace {
    const uint32_t Present = 1, Writable = 2, User = 4, Accessed = 0x20, Dirty = 0x40;
    const uint32_t AddressMask = 0xFFFFF000u;
    const uint32_t KernelPdeFlags = Present | Writable | Accessed;
    const uint32_t KernelPteFlags = Present | Writable | 0x18 | Accessed | Dirty | 0x200;
    const uint64_t AddressLimit = 0x100000000ULL;
#ifdef GTOS_PROCESS_MEMORY_TEST
    // The host harness substitutes hardware registers only, never mapping policy.
    extern "C" uint32_t gtos_process_memory_test_cr0, gtos_process_memory_test_cr3;
    extern "C" uint32_t gtos_process_memory_test_cr4;
    extern "C" bool gtos_process_memory_test_bsp;
#endif
    uint32_t CurrentDirectory() {
#ifdef GTOS_PROCESS_MEMORY_TEST
        return gtos_process_memory_test_cr3 & AddressMask;
#else
        uint32_t value; asm volatile("movl %%cr3,%0" : "=r"(value));
        return value & AddressMask;
#endif
    }
    bool SafeHardware() {
#ifdef GTOS_PROCESS_MEMORY_TEST
        return gtos_process_memory_test_bsp
            && (gtos_process_memory_test_cr0 & 0x80010000u) == 0x80010000u
            && !(gtos_process_memory_test_cr4 & (1u << 5));
#else
        uint32_t cr0, cr4;
        asm volatile("movl %%cr0,%0; movl %%cr4,%1" : "=r"(cr0), "=r"(cr4));
        if ((cr0 & 0x80010000u) != 0x80010000u || (cr4 & (1u << 5))) return false;
        uint32_t before, after;
        asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) : : "cc", "memory");
        if (!((before ^ after) & 0x200000)) return false;
        uint32_t a, b, c, d;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
        if (a < 1) return false;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        // No APs can be started by GTOS without APIC+MSR support. Refuse such
        // CPUs anyway rather than claiming that a caller's BSP identity is known.
        if ((d & ((1u << 5) | (1u << 9))) != ((1u << 5) | (1u << 9))) return false;
        uint32_t low, high;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
        return (low & (1u << 8)) != 0; // IA32_APIC_BASE.BSP, not an APIC-ID guess.
#endif
    }
    bool UserPage(uint32_t address) {
        return !(address & 4095) && address >= ProcessAddressSpace::UserBase
            && address < ProcessAddressSpace::UserLimit;
    }
    void ClearPage(uint32_t address) {
        uint32_t* page = (uint32_t*)address;
        for (uint32_t i = 0; i < 1024; ++i) page[i] = 0;
    }
    uint32_t PageEntry(uint32_t frame, bool writable) {
        return frame | Present | User | (writable ? Writable : 0);
    }
}
ProcessAddressSpace::ProcessAddressSpace() : kernel(0), frames(0), directory(0),
    kernelDirectory(0), pageCount(0), prepared(false), sealed(false), error(ProcessMemoryOk) {
    for (uint32_t i = 0; i < 1024; ++i) tables[i] = 0;
    for (uint32_t i = 0; i < MaximumPages; ++i) {
        pages[i].address = pages[i].frame = 0; pages[i].writable = false;
    }
}
bool ProcessAddressSpace::Fail(ProcessMemoryError value) const { error = value; return false; }
bool ProcessAddressSpace::Context(bool allowOwnDirectory) const {
    if (!SafeHardware()) return Fail(ProcessMemoryUnsafeContext);
    const uint32_t active = CurrentDirectory();
    if (directory && active == directory && !allowOwnDirectory) return Fail(ProcessMemoryActive);
    if (active != kernelDirectory && !(allowOwnDirectory && directory && active == directory))
        return Fail(ProcessMemoryUnsafeContext);
    return true;
}
bool ProcessAddressSpace::KernelAlias(uint32_t address, uint32_t length, bool writable) const {
    if (!address || !length || length > AddressLimit - address) return false;
    const uint64_t end = (uint64_t)address + length;
    for (uint64_t page = address & AddressMask; page < end; page += 4096) {
        PagingMapping mapping;
        if (!kernel->query((uint32_t)page, mapping) || mapping.physicalAddress != page
            || mapping.userAccessible || mapping.cacheDisabled || (writable && !mapping.writable))
            return false;
    }
    return true;
}
bool ProcessAddressSpace::AllocateFrame(uint32_t& address) {
    address = 0;
    if (!frames->allocate(address)) return Fail(ProcessMemoryNoMemory);
    if (!KernelAlias(address, 4096, true)) {
        frames->free(address); address = 0;
        return Fail(ProcessMemoryBadAlias);
    }
    ClearPage(address);
    return true;
}
bool ProcessAddressSpace::Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator) {
    InterruptGuard guard;
    if (prepared || directory || frames) return Fail(ProcessMemoryBadState);
    const PagingStatistics state = sharedKernel.getStatistics();
    if (!state.prepared || !state.enabled || !state.sealedForSharing
        || !sharedKernel.usesAllocator(allocator) || !allocator.getStatistics().initialized)
        return Fail(ProcessMemoryBadState);
    if (!SafeHardware() || CurrentDirectory() != state.directoryAddress)
        return Fail(ProcessMemoryUnsafeContext);
    kernel = &sharedKernel; kernelDirectory = state.directoryAddress;
    // The shared mapper is trusted, but its raw structures must satisfy the
    // supervisor-only non-PAE contract before any PDE is borrowed.
    bool valid = allocator.isAllocated(kernelDirectory)
        && KernelAlias(kernelDirectory, 4096, true)
        && KernelAlias((uint32_t)this, sizeof(*this), true)
        && KernelAlias((uint32_t)&allocator, sizeof(allocator), true)
        && KernelAlias((uint32_t)&sharedKernel, sizeof(sharedKernel), true);
    const uint32_t* source = (const uint32_t*)kernelDirectory;
    for (uint32_t di = 0; valid && di < 1024; ++di) {
        const uint32_t pde = source[di];
        if (!pde) continue;
        if (!(pde & Present) || (pde & 4095 & ~KernelPdeFlags)
            || !(pde & Writable) || (di >= UserBase >> 22 && di < UserLimit >> 22)) {
            valid = false; break;
        }
        const uint32_t table = pde & AddressMask;
        if (table == kernelDirectory || !allocator.isAllocated(table)
            || !KernelAlias(table, 4096, true)) { valid = false; break; }
        for (uint32_t previous = 0; previous < di; ++previous)
            if ((source[previous] & AddressMask) == table) valid = false;
        const uint32_t* entries = (const uint32_t*)table;
        for (uint32_t ti = 0; valid && ti < 1024; ++ti) {
            const uint32_t pte = entries[ti];
            if (pte && (!(pte & Present) || (pte & 4095 & ~KernelPteFlags))) valid = false;
        }
    }
    if (!valid) {
        kernel = 0; kernelDirectory = 0;
        return Fail(ProcessMemoryBadTemplate);
    }
    frames = &allocator;
    if (!AllocateFrame(directory)) {
        frames = 0; kernel = 0; kernelDirectory = 0;
        return false;
    }
    for (uint32_t i = 0; i < 1024; ++i) ((uint32_t*)directory)[i] = source[i];
    prepared = true;
    error = ProcessMemoryOk;
    return true;
}
int ProcessAddressSpace::FindPage(uint32_t address) const {
    for (uint32_t i = 0; i < pageCount; ++i) if (pages[i].address == address) return (int)i;
    return -1;
}
bool ProcessAddressSpace::Layout(bool complete) const {
    if (!prepared || !kernel || !frames || pageCount > MaximumPages)
        return Fail(ProcessMemoryBadState);
    const PagingStatistics state = kernel->getStatistics();
    if (!state.prepared || !state.enabled || !state.sealedForSharing
        || state.directoryAddress != kernelDirectory) return Fail(ProcessMemoryBadTemplate);
    if (!frames->isAllocated(directory) || !KernelAlias(directory, 4096, true)
        || !KernelAlias(kernelDirectory, 4096, true)) return Fail(ProcessMemoryBadAlias);
    const uint32_t* shared = (const uint32_t*)kernelDirectory;
    const uint32_t* own = (const uint32_t*)directory;
    for (uint32_t di = 0; di < 1024; ++di) {
        if (!tables[di]) {
            if ((!shared[di] && own[di])
                || (own[di] & ~Accessed) != (shared[di] & ~Accessed)) return Fail(ProcessMemoryBadMapping);
            if (di >= UserBase >> 22 && di < UserLimit >> 22 && shared[di])
                return Fail(ProcessMemoryBadTemplate);
            continue;
        }
        if (di < UserBase >> 22 || di >= UserLimit >> 22 || shared[di]
            || !frames->isAllocated(tables[di]) || !KernelAlias(tables[di], 4096, true)
            || (own[di] & ~Accessed) != (tables[di] | Present | Writable | User))
            return Fail(ProcessMemoryBadMapping);
        if (complete) {
            const uint32_t* table = (const uint32_t*)tables[di];
            for (uint32_t ti = 0; ti < 1024; ++ti) if (table[ti]) {
                const int index = FindPage((di << 22) | (ti << 12));
                if (index < 0 || (table[ti] & ~(Accessed | Dirty))
                    != PageEntry(pages[index].frame, pages[index].writable))
                    return Fail(ProcessMemoryBadMapping);
            }
        }
    }
    for (uint32_t i = 0; i < pageCount; ++i) {
        const Page& page = pages[i];
        const uint32_t table = tables[page.address >> 22];
        if (!UserPage(page.address) || !table || !frames->isAllocated(page.frame)
            || !KernelAlias(page.frame, 4096, true)
            || (((uint32_t*)table)[(page.address >> 12) & 1023] & ~(Accessed | Dirty))
                != PageEntry(page.frame, page.writable)) return Fail(ProcessMemoryBadMapping);
    }
    return true;
}
bool ProcessAddressSpace::Mutable() const {
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(false)) return false;
    if (sealed) return Fail(ProcessMemorySealed);
    return Layout(true);
}
bool ProcessAddressSpace::MapNewPage(uint32_t address, bool writable) {
    InterruptGuard guard;
    if (!Mutable()) return false;
    if (!UserPage(address)) return Fail(ProcessMemoryBadRange);
    if (FindPage(address) >= 0) return Fail(ProcessMemoryConflict);
    if (pageCount == MaximumPages) return Fail(ProcessMemoryLimit);
    const uint32_t di = address >> 22, ti = (address >> 12) & 1023;
    uint32_t table = tables[di], frame = 0;
    const bool newTable = !table;
    if (newTable && !AllocateFrame(table)) return false;
    if (!AllocateFrame(frame)) {
        if (newTable) frames->free(table);
        return false;
    }
    // No fallible operations follow publication. Both newly allocated pages
    // were zeroed through verified identity aliases before becoming visible.
    ((uint32_t*)table)[ti] = PageEntry(frame, writable);
    if (newTable) {
        tables[di] = table;
        ((uint32_t*)directory)[di] = table | Present | Writable | User;
    }
    Page& page = pages[pageCount++];
    page.address = address; page.frame = frame; page.writable = writable;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::ProtectPage(uint32_t address, bool writable) {
    InterruptGuard guard;
    if (!Mutable()) return false;
    if (!UserPage(address)) return Fail(ProcessMemoryBadRange);
    const int index = FindPage(address);
    if (index < 0) return Fail(ProcessMemoryNotMapped);
    Page& page = pages[index];
    uint32_t& entry = ((uint32_t*)tables[address >> 22])[(address >> 12) & 1023];
    entry = writable ? entry | Writable : entry & ~Writable;
    page.writable = writable;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::UnmapPage(uint32_t address) {
    InterruptGuard guard;
    if (!Mutable()) return false;
    if (!UserPage(address)) return Fail(ProcessMemoryBadRange);
    const int index = FindPage(address);
    if (index < 0) return Fail(ProcessMemoryNotMapped);
    const uint32_t di = address >> 22, frame = pages[index].frame;
    ((uint32_t*)tables[di])[(address >> 12) & 1023] = 0;
    pages[index] = pages[--pageCount];
    pages[pageCount].address = pages[pageCount].frame = 0; pages[pageCount].writable = false;
    // Ownership and all mappings were validated before any mutation. With BSP
    // interrupts masked the allocator cannot change between validation and free.
    ClearPage(frame); frames->free(frame);
    bool occupied = false;
    for (uint32_t i = 0; i < pageCount; ++i) if (pages[i].address >> 22 == di) occupied = true;
    if (!occupied) {
        ((uint32_t*)directory)[di] = 0;
        ClearPage(tables[di]); frames->free(tables[di]); tables[di] = 0;
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Seal() {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(false) || !Layout(true)) return false;
    sealed = true;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Range(uint32_t address, uint32_t length, bool writable) const {
    if (address < UserBase || address >= UserLimit || length > UserLimit - address)
        return Fail(ProcessMemoryBadRange);
    const uint64_t end = (uint64_t)address + length;
    if (!length) return true;
    for (uint64_t current = address & AddressMask; current < end; current += 4096) {
        const int index = FindPage((uint32_t)current);
        if (index < 0) return Fail(ProcessMemoryNotMapped);
        if (writable && !pages[index].writable) return Fail(ProcessMemoryPermission);
    }
    return true;
}
bool ProcessAddressSpace::Buffer(uint32_t address, uint32_t length, bool writable) const {
    if (!length) return true;
    if (!address || length > AddressLimit - address) return Fail(ProcessMemoryBadAlias);
    const uint64_t end = (uint64_t)address + length;
    for (uint64_t current = address & AddressMask; current < end; current += 4096) {
        PagingMapping mapping;
        if (!kernel->query((uint32_t)current, mapping) || mapping.userAccessible
            || mapping.cacheDisabled || (writable && !mapping.writable)) return Fail(ProcessMemoryBadAlias);
        const uint32_t physical = mapping.physicalAddress;
        if (physical == directory || physical == kernelDirectory) return Fail(ProcessMemoryBadAlias);
        for (uint32_t di = 0; di < 1024; ++di)
            if (physical == tables[di]
                || physical == (((uint32_t*)kernelDirectory)[di] & AddressMask)) return Fail(ProcessMemoryBadAlias);
        for (uint32_t i = 0; i < pageCount; ++i)
            if (physical == pages[i].frame) return Fail(ProcessMemoryBadAlias);
    }
    return true;
}
bool ProcessAddressSpace::ValidateUserRange(uint32_t address, uint32_t length, bool writable) const {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(true) || !Layout(false) || !Range(address, length, writable)) return false;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::CopyToUser(uint32_t destination, const void* source, uint32_t length) {
    InterruptGuard guard;
    if (!ValidateUserRange(destination, length, true) || !Buffer((uint32_t)source, length, false)) return false;
    const uint8_t* input = (const uint8_t*)source;
    while (length) {
        const uint32_t offset = destination & 4095;
        const uint32_t chunk = length < 4096 - offset ? length : 4096 - offset;
        uint8_t* output = (uint8_t*)(pages[FindPage(destination & AddressMask)].frame + offset);
        for (uint32_t i = 0; i < chunk; ++i) output[i] = input[i];
        destination += chunk; input += chunk; length -= chunk;
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::CopyFromUser(void* destination, uint32_t source, uint32_t length) const {
    InterruptGuard guard;
    if (!ValidateUserRange(source, length, false) || !Buffer((uint32_t)destination, length, true)) return false;
    uint8_t* output = (uint8_t*)destination;
    while (length) {
        const uint32_t offset = source & 4095;
        const uint32_t chunk = length < 4096 - offset ? length : 4096 - offset;
        const uint8_t* input = (const uint8_t*)(pages[FindPage(source & AddressMask)].frame + offset);
        for (uint32_t i = 0; i < chunk; ++i) output[i] = input[i];
        source += chunk; output += chunk; length -= chunk;
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Destroy() {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(false) || !Layout(true)) return false;
    // A sealed process is reclaimable ONLY after the runtime has removed all
    // references and switched CR3. This module cannot inspect scheduler queues.
    for (uint32_t i = 0; i < pageCount; ++i) {
        ClearPage(pages[i].frame); frames->free(pages[i].frame);
        pages[i].address = pages[i].frame = 0; pages[i].writable = false;
    }
    for (uint32_t i = 0; i < 1024; ++i) if (tables[i]) {
        ClearPage(tables[i]); frames->free(tables[i]); tables[i] = 0;
    }
    ClearPage(directory); frames->free(directory);
    directory = kernelDirectory = pageCount = 0;
    kernel = 0; frames = 0; prepared = sealed = false;
    error = ProcessMemoryOk;
    return true;
}
uint32_t ProcessAddressSpace::DirectoryAddress() const { return directory; }
bool ProcessAddressSpace::Prepared() const { return prepared; }
bool ProcessAddressSpace::Sealed() const { return sealed; }
ProcessMemoryError ProcessAddressSpace::GetLastError() const { return error; }
const char* ProcessAddressSpace::ErrorName(ProcessMemoryError value) {
    switch (value) {
        case ProcessMemoryOk: return "ok";
        case ProcessMemoryBadState: return "invalid process memory state";
        case ProcessMemoryBadRange: return "invalid user range or alignment";
        case ProcessMemoryNoMemory: return "no process frame memory";
        case ProcessMemoryConflict: return "user page already mapped";
        case ProcessMemoryNotMapped: return "unmapped user range";
        case ProcessMemoryPermission: return "user write to read-only page";
        case ProcessMemoryLimit: return "process page limit reached";
        case ProcessMemorySealed: return "process mappings sealed";
        case ProcessMemoryActive: return "process directory is active";
        case ProcessMemoryUnsafeContext: return "requires BSP, supported paging, and permitted CR3";
        case ProcessMemoryBadTemplate: return "invalid or overlapping kernel template";
        case ProcessMemoryBadMapping: return "process page tables inconsistent with ownership";
        case ProcessMemoryBadAlias: return "unsafe kernel or physical alias";
        default: return "unknown process memory error";
    }
}
