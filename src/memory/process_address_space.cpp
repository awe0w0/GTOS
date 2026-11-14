#include <memory/process_address_space.h>
#include <memory/criticalsection.h>
using namespace gtos::memory;
namespace {
    const uint32_t Present = 1, Writable = 2, User = 4, Accessed = 0x20, Dirty = 0x40;
    const uint32_t AddressMask = 0xFFFFF000u;
    const uint32_t KernelPdeFlags = Present | Writable | Accessed;
    const uint32_t KernelPteFlags = Present | Writable | 0x18 | Accessed | Dirty | 0x200;
    const uint64_t AddressLimit = 0x100000000ULL;
    // BSP interrupt guards serialize successful reservations across all owners.
    // This sequence deliberately survives Destroy() and process slot reuse.
    uint32_t nextRegionHandle = 1;
    // Ephemeral full-check workspace, not authoritative ownership. Every caller
    // holds the BSP interrupt guard; kernel mapping queries do not reenter Layout.
    const uint32_t DefaultIndexSlots = 8192;
    uint32_t defaultLayoutAddresses[DefaultIndexSlots], defaultLayoutFrames[DefaultIndexSlots];
    static_assert(ProcessAddressSpace::MaximumCapacityPages < DefaultIndexSlots, "bounded layout index load");
    uint32_t IndexSlots(uint32_t capacity) {
        if (capacity <= ProcessAddressSpace::MaximumCapacityPages) return DefaultIndexSlots;
        uint32_t slots = DefaultIndexSlots;
        while (slots < 2 * capacity) slots *= 2;
        return slots;
    }
    uint32_t LayoutHash(uint32_t address, uint32_t slots) {
        return ((address >> 12) * 2654435761U) & (slots - 1);
    }
#ifdef GTOS_PROCESS_MEMORY_TEST
    // The host harness substitutes hardware registers only, never mapping policy.
    extern "C" uint32_t gtos_process_memory_test_cr0, gtos_process_memory_test_cr3;
    extern "C" uint32_t gtos_process_memory_test_cr4;
    extern "C" bool gtos_process_memory_test_bsp;
    extern "C" void gtos_process_memory_test_invalidate(uint32_t address);
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
    kernelDirectory(0), pageCount(0), pages(inlinePages), pageLimit(MaximumPages),
    metadataBase(0), metadataPages(0), dynamicLimit(DynamicLimit), pendingPageCount(0), pendingTableCount(0),
    prepared(false), sealed(false), contentCopyLayoutValid(false), error(ProcessMemoryOk) {
    for (uint32_t i = 0; i < 1024; ++i) tables[i] = 0;
    for (uint32_t i = 0; i < MaximumPages; ++i) {
        pages[i].address = pages[i].frame = pages[i].region = 0;
        pages[i].writable = pages[i].present = false;
        pendingPages[i].address = pendingPages[i].frame = 0;
    }
    for (uint32_t i = 0; i < MaximumRegions; ++i)
        regions[i] = {};
    for (uint32_t i = 0; i < 2; ++i) pendingTables[i].index = pendingTables[i].frame = 0;
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
        if (!kernel->queryLocked((uint32_t)page, mapping) || mapping.physicalAddress != page
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
    return Prepare(sharedKernel, allocator, MaximumPages);
}
bool ProcessAddressSpace::Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                                  uint32_t capacityPages) {
    return Prepare(sharedKernel, allocator, capacityPages, DynamicLimit);
}
bool ProcessAddressSpace::Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                                  uint32_t capacityPages, uint32_t dynamicEnd) {
    return PrepareWithLimit(sharedKernel, allocator, capacityPages, dynamicEnd, MaximumCapacityPages);
}
bool ProcessAddressSpace::PrepareContent(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                                         uint32_t capacityPages, uint32_t dynamicEnd) {
    return PrepareWithLimit(sharedKernel, allocator, capacityPages, dynamicEnd, MaximumContentCapacityPages);
}
uint32_t ProcessAddressSpace::MetadataFrameCount(uint32_t capacityPages) {
    uint32_t bytes = capacityPages * sizeof(Page);
    if (capacityPages > MaximumCapacityPages) bytes += 2 * IndexSlots(capacityPages) * sizeof(uint32_t);
    return (bytes + 4095) / 4096;
}
bool ProcessAddressSpace::IsMetadataFrame(uint32_t address) const {
    return metadataBase && address >= metadataBase && address - metadataBase < metadataPages * 4096;
}
uint32_t* ProcessAddressSpace::LayoutIndex(bool byFrame) const {
    if (pageLimit <= MaximumCapacityPages) return byFrame ? defaultLayoutFrames : defaultLayoutAddresses;
    uint32_t* addresses = (uint32_t*)(metadataBase + pageLimit * sizeof(Page));
    return byFrame ? addresses + IndexSlots(pageLimit) : addresses;
}
bool ProcessAddressSpace::PrepareWithLimit(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator,
                                           uint32_t capacityPages, uint32_t dynamicEnd, uint32_t maximumPages) {
    InterruptGuard guard;
    if (prepared || directory || frames) return Fail(ProcessMemoryBadState);
    if (capacityPages < MaximumPages || capacityPages > maximumPages)
        return Fail(ProcessMemoryLimit);
    if ((dynamicEnd & 4095) || dynamicEnd <= DynamicBase || dynamicEnd > DynamicLimit)
        return Fail(ProcessMemoryBadRange);
    const PagingStatistics state = sharedKernel.getStatistics();
    if (!state.prepared || !state.enabled || !state.sealedForSharing
        || !sharedKernel.usesAllocator(allocator) || !allocator.getStatistics().initialized)
        return Fail(ProcessMemoryBadState);
    if (!SafeHardware() || CurrentDirectory() != state.directoryAddress)
        return Fail(ProcessMemoryUnsafeContext);
    kernel = &sharedKernel; kernelDirectory = state.directoryAddress;
    // The shared mapper is trusted, but its raw structures must satisfy the
    // supervisor-only non-PAE contract before any PDE is borrowed.
    bool valid = allocator.isAllocatedLocked(kernelDirectory)
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
        if (table == kernelDirectory || !allocator.isAllocatedLocked(table)
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
    if (capacityPages > MaximumPages) {
        const uint32_t count = MetadataFrameCount(capacityPages);
        uint32_t base = 0;
        if (!frames->allocateContiguous(count, base)) {
            frames = 0; kernel = 0; kernelDirectory = 0;
            return Fail(ProcessMemoryNoMemory);
        }
        if (!KernelAlias(base, count * 4096, true)) {
            frames->freeContiguous(base, count);
            frames = 0; kernel = 0; kernelDirectory = 0;
            return Fail(ProcessMemoryBadAlias);
        }
        for (uint32_t i = 0; i < count; ++i) ClearPage(base + i * 4096);
        metadataBase = base; metadataPages = count;
        pages = (Page*)base; pageLimit = capacityPages;
    } else {
        // The inline case has to establish the ownership array itself. Prepare already writes dynamicLimit, because it cannot
        // assume the constructor ran: a freestanding harness enters with zeroed storage, and MetadataLayout then rejects the
        // space it just built because pageLimit is zero and pages does not point at the inline array.
        pages = inlinePages; pageLimit = capacityPages;
    }
    if (!AllocateFrame(directory)) {
        if (metadataBase) {
            frames->freeContiguous(metadataBase, metadataPages);
            metadataBase = metadataPages = 0; pages = inlinePages; pageLimit = MaximumPages;
        }
        frames = 0; kernel = 0; kernelDirectory = 0;
        return false;
    }
    for (uint32_t i = 0; i < 1024; ++i) ((uint32_t*)directory)[i] = source[i];
    dynamicLimit = dynamicEnd; prepared = true; contentCopyLayoutValid = false;
    error = ProcessMemoryOk;
    return true;
}
int ProcessAddressSpace::IndexedLayoutPage(uint32_t key, bool byFrame) const {
    const uint32_t* entries = LayoutIndex(byFrame);
    const uint32_t slots = IndexSlots(pageLimit);
    uint32_t slot = LayoutHash(key, slots);
    for (uint32_t count = 0; count < slots; ++count) {
        const uint32_t record = entries[slot];
        if (!record || record > pageCount) return -1;
        const Page& page = pages[record - 1];
        if ((byFrame ? page.frame : page.address) == key) return (int)(record - 1);
        slot = (slot + 1) & (slots - 1);
    }
    return -1;
}
int ProcessAddressSpace::FindRegion(uint32_t handle) const {
    if (!handle || handle > 0x7FFFFFFFu) return -1;
    for (uint32_t i = 0; i < MaximumRegions; ++i)
        if (regions[i].handle == handle) return (int)i;
    return -1;
}
bool ProcessAddressSpace::SharesFrame(const Page& page) const {
    const int index = FindRegion(page.region);
    return index >= 0 && regions[index].sharedFrames;
}
bool ProcessAddressSpace::MetadataLayout() const {
    if (pageLimit < MaximumPages || pageLimit > MaximumContentCapacityPages) return false;
    if (!metadataBase)
        return pages == inlinePages && !metadataPages && pageLimit == MaximumPages;
    if (pages != (Page*)metadataBase || (metadataBase & 4095) || pageLimit == MaximumPages
        || metadataPages != MetadataFrameCount(pageLimit)
        || metadataPages * 4096 > AddressLimit - metadataBase
        || !KernelAlias(metadataBase, metadataPages * 4096, true)) return false;
    if (IsMetadataFrame(directory) || IsMetadataFrame(kernelDirectory)) return false;
    for (uint32_t i = 0; i < metadataPages; ++i)
        if (!frames->isAllocatedLocked(metadataBase + i * 4096)) return false;
    for (uint32_t di = 0; di < 1024; ++di)
        if (IsMetadataFrame(tables[di]) || IsMetadataFrame(((uint32_t*)kernelDirectory)[di] & AddressMask))
            return false;
    return true;
}
bool ProcessAddressSpace::LayoutRoot() const {
    const PagingStatistics state = kernel->getStatistics();
    if (!state.prepared || !state.enabled || !state.sealedForSharing
        || state.directoryAddress != kernelDirectory) return Fail(ProcessMemoryBadTemplate);
    if (!frames->isAllocatedLocked(directory) || !KernelAlias(directory, 4096, true)
        || !KernelAlias(kernelDirectory, 4096, true)) return Fail(ProcessMemoryBadAlias);
    return true;
}
bool ProcessAddressSpace::LayoutDirectories(uint32_t* nonzeroLeaves) const {
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
            || !frames->isAllocatedLocked(tables[di]) || !KernelAlias(tables[di], 4096, true)
            || (own[di] & ~Accessed) != (tables[di] | Present | Writable | User))
            return Fail(ProcessMemoryBadMapping);
        if (nonzeroLeaves) {
            const uint32_t* table = (const uint32_t*)tables[di];
            for (uint32_t ti = 0; ti < 1024; ++ti) *nonzeroLeaves += table[ti] != 0;
        }
    }
    return true;
}
bool ProcessAddressSpace::Layout(bool complete) const {
    contentCopyLayoutValid = false;
    if (!prepared || !kernel || !frames || pageCount > pageLimit
        || (dynamicLimit & 4095) || dynamicLimit <= DynamicBase || dynamicLimit > DynamicLimit
        || pendingPageCount || pendingTableCount)
        return Fail(ProcessMemoryBadState);
    for (uint32_t i = 0; i < MaximumRegions; ++i) {
        const Region& region = regions[i];
        if (!region.handle) {
            if (region.base || region.bytes || region.sharedFrames) return Fail(ProcessMemoryBadMapping);
            continue;
        }
        if (!sealed || region.handle > 0x7FFFFFFFu || (region.base & 4095)
            || !region.bytes || (region.bytes & 4095) || region.base < DynamicBase
            || region.base >= dynamicLimit || region.bytes > dynamicLimit - region.base
            || (region.sharedFrames & 4095))
            return Fail(ProcessMemoryBadMapping);
        for (uint32_t previous = 0; previous < i; ++previous) {
            const Region& other = regions[previous];
            if (other.handle && (other.handle == region.handle
                || (region.base < other.base + other.bytes
                    && other.base < region.base + region.bytes))) return Fail(ProcessMemoryBadMapping);
        }
    }
    if (!LayoutRoot()) return false;
    if (!MetadataLayout()) return Fail(ProcessMemoryBadAlias);
    const uint32_t slots = IndexSlots(pageLimit);
    uint32_t* layoutAddresses = LayoutIndex(false);
    uint32_t* layoutFrames = LayoutIndex(true);
    for (uint32_t i = 0; i < slots; ++i) layoutAddresses[i] = layoutFrames[i] = 0;
    for (uint32_t i = 0; i < pageCount; ++i) {
        const Page& page = pages[i];
        uint32_t slot = LayoutHash(page.address, slots), count = 0;
        while (layoutAddresses[slot]) {
            if (pages[layoutAddresses[slot] - 1].address == page.address || ++count == slots)
                return Fail(ProcessMemoryBadMapping);
            slot = (slot + 1) & (slots - 1);
        }
        layoutAddresses[slot] = i + 1;
        slot = LayoutHash(page.frame, slots); count = 0;
        while (layoutFrames[slot]) {
            const Page& other = pages[layoutFrames[slot] - 1];
            if (other.frame == page.frame) {
                if (!SharesFrame(page) || !SharesFrame(other)) return Fail(ProcessMemoryBadMapping);
                break;
            }
            if (++count == slots)
                return Fail(ProcessMemoryBadMapping);
            slot = (slot + 1) & (slots - 1);
        }
        layoutFrames[slot] = i + 1;
    }
    uint32_t nonzeroLeaves = 0, presentPages = 0;
    if (!LayoutDirectories(complete ? &nonzeroLeaves : nullptr)) return false;
    uint16_t sharedCounts[MaximumRegions] = {};
    for (uint32_t i = 0; i < pageCount; ++i) {
        const Page& page = pages[i];
        if (page.region) {
            const int index = FindRegion(page.region);
            if (index < 0 || page.address < regions[index].base
                || page.address >= regions[index].base + regions[index].bytes)
                return Fail(ProcessMemoryBadMapping);
            const Region& region = regions[index];
            if (region.sharedFrames) {
                if (!page.present || page.frame != region.sharedFrames + (page.address - region.base))
                    return Fail(ProcessMemoryBadMapping);
                ++sharedCounts[index];
            }
        } else {
            if (!page.present) return Fail(ProcessMemoryBadMapping);
            // Every validated reservation lies in the dynamic address range.
            if (page.address >= DynamicBase && page.address < dynamicLimit) {
                for (uint32_t ri = 0; ri < MaximumRegions; ++ri)
                    if (regions[ri].handle && page.address >= regions[ri].base
                        && page.address < regions[ri].base + regions[ri].bytes)
                        return Fail(ProcessMemoryBadMapping);
            }
        }
        if (!page.present && page.writable) return Fail(ProcessMemoryBadMapping);
        const uint32_t table = tables[page.address >> 22];
        if (!UserPage(page.address) || !table || !frames->isAllocatedLocked(page.frame)
            || IsMetadataFrame(page.frame)
            || !KernelAlias(page.frame, 4096, true)
            || (((uint32_t*)table)[(page.address >> 12) & 1023] & ~(Accessed | Dirty))
                != (page.present ? PageEntry(page.frame, page.writable) : 0))
            return Fail(ProcessMemoryBadMapping);
        presentPages += page.present;
    }
    // Unique addresses and exact leaves above account for every present page.
    // A larger table count means an unowned or nonpresent nonzero entry.
    if (complete && presentPages != nonzeroLeaves) return Fail(ProcessMemoryBadMapping);
    for (uint32_t i = 0; i < MaximumRegions; ++i)
        if (regions[i].sharedFrames && sharedCounts[i] != regions[i].bytes / 4096)
            return Fail(ProcessMemoryBadMapping);
    contentCopyLayoutValid = pageLimit > MaximumCapacityPages;
    return true;
}
bool ProcessAddressSpace::Mutable() const {
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(false)) return false;
    if (sealed) return Fail(ProcessMemorySealed);
    return Layout(true);
}
bool ProcessAddressSpace::RuntimeMutable() const {
    if (!prepared || !sealed) return Fail(ProcessMemoryBadState);
    if (!Context(true)) return false;
#ifndef GTOS_PROCESS_MEMORY_TEST
    if (CurrentDirectory() != directory) return Fail(ProcessMemoryUnsafeContext);
#endif
    return Layout(true);
}
bool ProcessAddressSpace::MapOwnedPage(uint32_t address, bool writable) {
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
    contentCopyLayoutValid = false;
    ((uint32_t*)table)[ti] = PageEntry(frame, writable);
    if (newTable) {
        tables[di] = table;
        ((uint32_t*)directory)[di] = table | Present | Writable | User;
    }
    Page& page = pages[pageCount++];
    page.address = address; page.frame = frame; page.region = 0;
    page.writable = writable; page.present = true;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::MapNewPage(uint32_t address, bool writable) {
    return MapNewRange(address, 4096, writable);
}
bool ProcessAddressSpace::MapNewRange(uint32_t address, uint32_t bytes, bool writable) {
    InterruptGuard guard;
    if (!Mutable()) return false;
    if (!UserPage(address) || !bytes || (bytes & 4095) || bytes > UserLimit - address)
        return Fail(ProcessMemoryBadRange);
    if (bytes / 4096 > pageLimit - pageCount) return Fail(ProcessMemoryLimit);
    const uint32_t end = address + bytes, before = pageCount;
    for (uint32_t page = address; page < end; page += 4096)
        if (IndexedLayoutPage(page) >= 0) return Fail(ProcessMemoryConflict);
    for (uint32_t page = address; page < end; page += 4096) {
        if (!MapOwnedPage(page, writable)) {
            while (pageCount > before) RemovePage(pageCount - 1);
            return false;
        }
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::ProtectPage(uint32_t address, bool writable) {
    return ProtectRange(address, 4096, writable);
}
#ifdef GTOS_PROCESS_MEMORY_TEST
// A host-only fault point, declared and inert unless the host-test macro is defined. It exists to reach the failure of a
// LATE stage of Create, after earlier stages have already taken frames and mapped pages: without it a unit can only
// starve the allocator, which fails the first stage and therefore never exercises the rollback of a partial acquisition.
// The default profile does not define this macro, so the production path is unchanged.
//
// The witness is part of the seam, not of the test. Every call to ProtectRange records that it was consulted, the page
// count it saw, and whether it refused. A test can then ask the executed path what happened instead of inferring it from
// the return value: "Create returned false" is consistent with failing anywhere, but a recorded consultation that
// refused at page count 2 can only be this stage.
extern "C" uint32_t gtos_process_memory_test_fail_protect_after;
extern "C" uint32_t gtos_process_memory_test_protect_calls;
extern "C" uint32_t gtos_process_memory_test_protect_refusals;
extern "C" uint32_t gtos_process_memory_test_protect_pages_at_refusal;
#endif
bool ProcessAddressSpace::ProtectRange(uint32_t address, uint32_t bytes, bool writable) {
    InterruptGuard guard;
    if (!Mutable()) return false;
#ifdef GTOS_PROCESS_MEMORY_TEST
    ++gtos_process_memory_test_protect_calls;
    if (gtos_process_memory_test_fail_protect_after && pageCount >= gtos_process_memory_test_fail_protect_after) {
        // The seam refuses at this stage, which is the fifth of Create. Everything before it already happened, and the
        // page count recorded here is what it had already acquired.
        ++gtos_process_memory_test_protect_refusals;
        gtos_process_memory_test_protect_pages_at_refusal = pageCount;
        return Fail(ProcessMemoryNoMemory);
    }
#endif
    if (!UserPage(address) || !bytes || (bytes & 4095) || bytes > UserLimit - address)
        return Fail(ProcessMemoryBadRange);
    if (bytes / 4096 > pageLimit) return Fail(ProcessMemoryNotMapped);
    const uint32_t end = address + bytes;
    for (uint32_t page = address; page < end; page += 4096)
        if (IndexedLayoutPage(page) < 0) return Fail(ProcessMemoryNotMapped);
    contentCopyLayoutValid = false;
    for (uint32_t page = address; page < end; page += 4096) {
        Page& owned = pages[IndexedLayoutPage(page)];
        uint32_t& entry = ((uint32_t*)tables[page >> 22])[(page >> 12) & 1023];
        entry = writable ? entry | Writable : entry & ~Writable;
        owned.writable = writable;
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::UnmapPage(uint32_t address) {
    InterruptGuard guard;
    if (!Mutable()) return false;
    if (!UserPage(address)) return Fail(ProcessMemoryBadRange);
    const int index = IndexedLayoutPage(address);
    if (index < 0) return Fail(ProcessMemoryNotMapped);
    RemovePage((uint32_t)index);
    error = ProcessMemoryOk;
    return true;
}
void ProcessAddressSpace::RemovePage(uint32_t index) {
    contentCopyLayoutValid = false;
    const uint32_t address = pages[index].address;
    const uint32_t di = address >> 22, frame = pages[index].frame;
    ((uint32_t*)tables[di])[(address >> 12) & 1023] = 0;
    pages[index] = pages[--pageCount];
    pages[pageCount].address = pages[pageCount].frame = pages[pageCount].region = 0;
    pages[pageCount].writable = pages[pageCount].present = false;
    // Ownership and all mappings were validated before any mutation. With BSP
    // interrupts masked the allocator cannot change between validation and free.
    ClearPage(frame); frames->free(frame);
    bool occupied = false;
    for (uint32_t i = 0; i < pageCount; ++i) if (pages[i].address >> 22 == di) occupied = true;
    if (!occupied) {
        ((uint32_t*)directory)[di] = 0;
        ClearPage(tables[di]); frames->free(tables[di]); tables[di] = 0;
    }
}
bool ProcessAddressSpace::Seal() {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(false) || !Layout(true)) return false;
    sealed = true;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::RegionRange(uint32_t handle, uint32_t offset, uint32_t bytes,
                                      uint32_t& base) const {
    const int index = FindRegion(handle);
    if (index < 0) return Fail(ProcessMemoryBadState);
    const Region& region = regions[index];
    if (region.sharedFrames) return Fail(ProcessMemoryPermission);
    if ((offset & 4095) || !bytes || (bytes & 4095) || offset > region.bytes
        || bytes > region.bytes - offset) return Fail(ProcessMemoryBadRange);
    base = region.base + offset;
    return true;
}
uint32_t ProcessAddressSpace::ConflictEnd(uint32_t base, uint32_t bytes) const {
    uint32_t end = 0;
    for (uint32_t i = 0; i < MaximumRegions; ++i) {
        const Region& region = regions[i];
        if (region.handle && base < region.base + region.bytes && region.base < base + bytes
            && region.base + region.bytes > end) end = region.base + region.bytes;
    }
    for (uint32_t i = 0; i < pageCount; ++i) {
        const Page& page = pages[i];
        if (base < page.address + 4096 && page.address < base + bytes
            && page.address + 4096 > end) end = page.address + 4096;
    }
    return end;
}
bool ProcessAddressSpace::Reserve(uint32_t bytes, uint32_t alignment, uint32_t hint,
                                  uint32_t& outBase, uint32_t& outHandle) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    if (!bytes || (bytes & 4095) || bytes > dynamicLimit - DynamicBase
        || alignment < 4096 || (alignment & (alignment - 1))) return Fail(ProcessMemoryBadRange);
    if (hint && ((hint & (alignment - 1)) || hint < DynamicBase || hint >= dynamicLimit
        || bytes > dynamicLimit - hint)) return Fail(ProcessMemoryBadRange);
    int slot = -1;
    for (uint32_t i = 0; i < MaximumRegions; ++i)
        if (!regions[i].handle) { slot = (int)i; break; }
    if (slot < 0) return Fail(ProcessMemoryRegionLimit);
    if (nextRegionHandle > 0x7FFFFFFFu) return Fail(ProcessMemoryHandleExhausted);
    uint64_t candidate = hint ? hint
        : ((uint64_t)DynamicBase + alignment - 1) & ~((uint64_t)alignment - 1);
    while (candidate <= dynamicLimit - bytes) {
        const uint32_t conflict = ConflictEnd((uint32_t)candidate, bytes);
        if (!conflict) break;
        if (hint) return Fail(ProcessMemoryConflict);
        candidate = ((uint64_t)conflict + alignment - 1) & ~((uint64_t)alignment - 1);
    }
    if (candidate > dynamicLimit - bytes) return Fail(ProcessMemoryConflict);
    contentCopyLayoutValid = false;
    Region& region = regions[slot];
    region.base = (uint32_t)candidate; region.bytes = bytes;
    region.handle = nextRegionHandle++;
    outBase = region.base; outHandle = region.handle;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::MapSharedFrames(uint32_t firstFrame, uint32_t bytes, bool writable,
                                          uint32_t& outBase, uint32_t& outHandle) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    // A four MiB mapping spans at most two PDEs, even with an unaligned base.
    if (!firstFrame || !bytes || ((firstFrame | bytes) & 4095) || bytes > (1U << 22))
        return Fail(ProcessMemoryBadRange);
    if (bytes / 4096 > pageLimit - pageCount) return Fail(ProcessMemoryLimit);
    if (!KernelAlias(firstFrame, bytes, true)) return Fail(ProcessMemoryBadAlias);
    const auto contains = [firstFrame, bytes](uint32_t frame) {
        return frame >= firstFrame && frame - firstFrame < bytes;
    };
    if (contains(directory) || contains(kernelDirectory)
        || (metadataBase && (uint64_t)metadataBase < (uint64_t)firstFrame + bytes
            && (uint64_t)firstFrame < (uint64_t)metadataBase + metadataPages * 4096))
        return Fail(ProcessMemoryBadAlias);
    const uint32_t* shared = (const uint32_t*)kernelDirectory;
    for (uint32_t di = 0; di < 1024; ++di)
        if (contains(tables[di]) || contains(shared[di] & AddressMask))
            return Fail(ProcessMemoryBadAlias);
    for (uint32_t i = 0; i < pageCount; ++i)
        if (contains(pages[i].frame) && !SharesFrame(pages[i]))
            return Fail(ProcessMemoryBadAlias);
    for (uint32_t offset = 0; offset < bytes; offset += 4096)
        if (!frames->isAllocatedLocked(firstFrame + offset)) return Fail(ProcessMemoryBadAlias);

    uint32_t base, handle;
    if (!Reserve(bytes, 4096, 0, base, handle)) return false;
    Region& region = regions[FindRegion(handle)];
    for (uint32_t di = base >> 22; di <= (base + bytes - 1) >> 22; ++di) {
        if (tables[di]) continue;
        uint32_t table;
        if (!AllocateFrame(table)) {
            RollbackPending();
            region = {};
            --nextRegionHandle;
            return false;
        }
        pendingTables[pendingTableCount++] = {di, table};
    }
    for (uint32_t i = 0; i < pendingTableCount; ++i) {
        const PendingTable& pending = pendingTables[i];
        tables[pending.index] = pending.frame;
        ((uint32_t*)directory)[pending.index] = pending.frame | Present | Writable | User;
    }
    for (uint32_t offset = 0; offset < bytes; offset += 4096) {
        const uint32_t address = base + offset, frame = firstFrame + offset;
        pages[pageCount++] = {address, frame, handle, writable, true};
        ((uint32_t*)tables[address >> 22])[(address >> 12) & 1023] = PageEntry(frame, writable);
        InvalidatePage(address);
    }
    for (uint32_t i = 0; i < pendingTableCount; ++i) pendingTables[i] = {};
    pendingTableCount = 0;
    region.sharedFrames = firstFrame;
    outBase = base; outHandle = handle;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::UnmapSharedFrames(uint32_t handle) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    const int index = FindRegion(handle);
    if (index < 0 || !regions[index].sharedFrames) return Fail(ProcessMemoryBadState);
    Region& region = regions[index];
    RemoveRegionPages(handle, region.base, region.bytes, false);
    region = {};
    error = ProcessMemoryOk;
    return true;
}
void ProcessAddressSpace::InvalidatePage(uint32_t address) const {
    if (CurrentDirectory() != directory) return;
#ifdef GTOS_PROCESS_MEMORY_TEST
    gtos_process_memory_test_invalidate(address);
#else
    asm volatile("invlpg (%0)" : : "r"(address) : "memory");
#endif
}
void ProcessAddressSpace::RollbackPending() {
    for (uint32_t i = 0; i < pendingPageCount; ++i) {
        ClearPage(pendingPages[i].frame); frames->free(pendingPages[i].frame);
        pendingPages[i].address = pendingPages[i].frame = 0;
    }
    for (uint32_t i = 0; i < pendingTableCount; ++i) {
        ClearPage(pendingTables[i].frame); frames->free(pendingTables[i].frame);
        pendingTables[i].index = pendingTables[i].frame = 0;
    }
    pendingPageCount = pendingTableCount = 0;
}
bool ProcessAddressSpace::SetPermissions(uint32_t handle, uint32_t offset, uint32_t bytes,
                                        uint32_t protection) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    if (protection != ProcessMemoryNone && protection != ProcessMemoryRead
        && protection != ProcessMemoryReadWrite) return Fail(ProcessMemoryPermission);
    uint32_t base;
    if (!RegionRange(handle, offset, bytes, base)) return false;
    contentCopyLayoutValid = false;
    const uint32_t end = base + bytes;
    if (protection != ProcessMemoryNone) {
        // Every page will be resident afterwards; reject oversized sparse
        // commits before walking or allocating their otherwise empty range.
        if (bytes / 4096 > MaximumPages) return Fail(ProcessMemoryLimit);
        uint32_t missing = 0;
        for (uint32_t address = base; address < end; address += 4096)
            if (IndexedLayoutPage(address) < 0) ++missing;
        if (missing > pageLimit - pageCount) return Fail(ProcessMemoryLimit);
        for (uint32_t address = base; address < end; address += 4096) {
            if (IndexedLayoutPage(address) >= 0) continue;
            const uint32_t di = address >> 22;
            bool newTable = !tables[di];
            for (uint32_t i = 0; i < pendingTableCount; ++i)
                if (pendingTables[i].index == di) newTable = false;
            if (newTable) {
                uint32_t table;
                if (!AllocateFrame(table)) { RollbackPending(); return false; }
                PendingTable& pending = pendingTables[pendingTableCount++];
                pending.index = di; pending.frame = table;
            }
            uint32_t frame;
            if (!AllocateFrame(frame)) { RollbackPending(); return false; }
            PendingPage& pending = pendingPages[pendingPageCount++];
            pending.address = address; pending.frame = frame;
        }
        // No fallible operation follows. Publish only after all allocation,
        // alias validation, and zeroing succeeds, including across PDE edges.
        for (uint32_t i = 0; i < pendingTableCount; ++i) {
            const PendingTable& pending = pendingTables[i];
            tables[pending.index] = pending.frame;
            ((uint32_t*)directory)[pending.index] = pending.frame | Present | Writable | User;
        }
        for (uint32_t i = 0; i < pendingPageCount; ++i) {
            Page& page = pages[pageCount++];
            page.address = pendingPages[i].address; page.frame = pendingPages[i].frame;
            page.region = handle; page.present = true;
            page.writable = protection == ProcessMemoryReadWrite;
            pendingPages[i].address = pendingPages[i].frame = 0;
        }
        for (uint32_t i = 0; i < pendingTableCount; ++i)
            pendingTables[i].index = pendingTables[i].frame = 0;
        pendingPageCount = pendingTableCount = 0;
    }
    for (uint32_t i = 0; i < pageCount; ++i) {
        Page& page = pages[i];
        if (page.region != handle || page.address < base || page.address >= end) continue;
        page.present = protection != ProcessMemoryNone;
        page.writable = protection == ProcessMemoryReadWrite;
        uint32_t& entry = ((uint32_t*)tables[page.address >> 22])[(page.address >> 12) & 1023];
        const uint32_t expected = page.present ? PageEntry(page.frame, page.writable) : 0;
        if ((entry & ~(Accessed | Dirty)) != expected) {
            entry = expected;
            InvalidatePage(page.address);
        }
    }
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::SetDataReadOnly(uint32_t address, uint32_t bytes) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    if (!bytes || ((address | bytes) & 4095) || address < UserBase
        || address >= UserLimit || bytes > UserLimit - address)
        return Fail(ProcessMemoryBadRange);
    if (bytes / 4096 > pageLimit) return Fail(ProcessMemoryNotMapped);
    const uint32_t end = address + bytes;
    for (uint32_t page = address; page < end; page += 4096) {
        const int index = IndexedLayoutPage(page);
        if (index < 0 || !pages[index].present) return Fail(ProcessMemoryNotMapped);
        if (pages[index].region) return Fail(ProcessMemoryPermission);
    }
    // Layout and every page were checked. No fallible operation follows.
    contentCopyLayoutValid = false;
    for (uint32_t i = 0; i < pageCount; ++i) {
        Page& page = pages[i];
        if (page.address < address || page.address >= end || !page.writable) continue;
        uint32_t& entry = ((uint32_t*)tables[page.address >> 22])[(page.address >> 12) & 1023];
        entry &= ~Writable;
        page.writable = false;
        InvalidatePage(page.address);
    }
    error = ProcessMemoryOk;
    return true;
}
void ProcessAddressSpace::RemoveRegionPages(uint32_t handle, uint32_t base, uint32_t bytes,
                                            bool releaseFrames) {
    contentCopyLayoutValid = false;
    const uint32_t end = base + bytes;
    uint32_t index = 0;
    while (index < pageCount) {
        const Page& page = pages[index];
        if (page.region != handle || page.address < base || page.address >= end) { ++index; continue; }
        const uint32_t address = page.address, frame = page.frame, di = address >> 22;
        const uint32_t table = tables[di];
        ((uint32_t*)table)[(address >> 12) & 1023] = 0;
        pages[index] = pages[--pageCount];
        pages[pageCount].address = pages[pageCount].frame = pages[pageCount].region = 0;
        pages[pageCount].writable = pages[pageCount].present = false;
        bool occupied = false;
        for (uint32_t i = 0; i < pageCount; ++i)
            if (pages[i].address >> 22 == di) occupied = true;
        if (!occupied) ((uint32_t*)directory)[di] = 0;
        // Flush both leaf and paging-structure caches before either data or
        // the last owned table can be freed and reused by another reservation.
        InvalidatePage(address);
        if (releaseFrames) { ClearPage(frame); frames->free(frame); }
        if (!occupied) {
            ClearPage(table); frames->free(table); tables[di] = 0;
        }
    }
}
bool ProcessAddressSpace::Decommit(uint32_t handle, uint32_t offset, uint32_t bytes) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    uint32_t base;
    if (!RegionRange(handle, offset, bytes, base)) return false;
    RemoveRegionPages(handle, base, bytes);
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Discard(uint32_t handle, uint32_t offset, uint32_t bytes) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    uint32_t base;
    if (!RegionRange(handle, offset, bytes, base)) return false;
    if (bytes / 4096 > MaximumPages) return Fail(ProcessMemoryNotMapped);
    const uint32_t end = base + bytes;
    for (uint32_t address = base; address < end; address += 4096) {
        const int index = IndexedLayoutPage(address);
        if (index < 0 || pages[index].region != handle) return Fail(ProcessMemoryNotMapped);
    }
    for (uint32_t i = 0; i < pageCount; ++i)
        if (pages[i].region == handle && pages[i].address >= base && pages[i].address < end)
            ClearPage(pages[i].frame);
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Release(uint32_t handle) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    const int index = FindRegion(handle);
    if (index < 0) return Fail(ProcessMemoryBadState);
    Region& region = regions[index];
    if (region.sharedFrames) return Fail(ProcessMemoryPermission);
    RemoveRegionPages(handle, region.base, region.bytes);
    region = {};
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Trim(uint32_t handle, uint32_t newBytes) {
    InterruptGuard guard;
    if (!RuntimeMutable()) return false;
    const int index = FindRegion(handle);
    if (index < 0) return Fail(ProcessMemoryBadState);
    Region& region = regions[index];
    if (region.sharedFrames) return Fail(ProcessMemoryPermission);
    if (!newBytes || (newBytes & 4095) || newBytes > region.bytes) return Fail(ProcessMemoryBadRange);
    RemoveRegionPages(handle, region.base + newBytes, region.bytes - newBytes);
    region.bytes = newBytes;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::QueryRegion(uint32_t handle, ProcessMemoryRegionInfo& info) const {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(true) || !Layout(true)) return false;
    const int index = FindRegion(handle);
    if (index < 0) return Fail(ProcessMemoryBadState);
    uint32_t resident = 0;
    for (uint32_t i = 0; i < pageCount; ++i) if (pages[i].region == handle) ++resident;
    info.base = regions[index].base; info.bytes = regions[index].bytes; info.residentPages = resident;
    error = ProcessMemoryOk;
    return true;
}
bool ProcessAddressSpace::Range(uint32_t address, uint32_t length, bool writable) const {
    if (address < UserBase || address >= UserLimit || length > UserLimit - address)
        return Fail(ProcessMemoryBadRange);
    const uint64_t end = (uint64_t)address + length;
    if (!length) return true;
    for (uint64_t current = address & AddressMask; current < end; current += 4096) {
        const int index = IndexedLayoutPage((uint32_t)current);
        if (index < 0) return Fail(ProcessMemoryNotMapped);
        const Page& page = pages[index];
        if (!page.present || (writable && !page.writable)) return Fail(ProcessMemoryPermission);
        if (pageLimit > MaximumCapacityPages) {
            // The cached record is only an index. Revalidate the live leaf and
            // its supervisor alias before using the physical frame for a copy.
            const uint32_t table = tables[page.address >> 22];
            if (!table || !frames->isAllocatedLocked(page.frame) || IsMetadataFrame(page.frame)
                || !KernelAlias(page.frame, 4096, true)
                || (((uint32_t*)table)[(page.address >> 12) & 1023] & ~(Accessed | Dirty))
                    != PageEntry(page.frame, page.writable)) {
                contentCopyLayoutValid = false;
                return Fail(ProcessMemoryBadMapping);
            }
        }
    }
    return true;
}
bool ProcessAddressSpace::Buffer(uint32_t address, uint32_t length, bool writable) const {
    if (!length) return true;
    if (!address || length > AddressLimit - address) return Fail(ProcessMemoryBadAlias);
    const uint64_t end = (uint64_t)address + length;
    for (uint64_t current = address & AddressMask; current < end; current += 4096) {
        PagingMapping mapping;
        if (!kernel->queryLocked((uint32_t)current, mapping) || mapping.userAccessible
            || mapping.cacheDisabled || (writable && !mapping.writable)) return Fail(ProcessMemoryBadAlias);
        const uint32_t physical = mapping.physicalAddress;
        if (IsMetadataFrame(physical)) return Fail(ProcessMemoryBadAlias);
        if (physical == directory || physical == kernelDirectory) return Fail(ProcessMemoryBadAlias);
        for (uint32_t di = 0; di < 1024; ++di)
            if (physical == tables[di]
                || physical == (((uint32_t*)kernelDirectory)[di] & AddressMask)) return Fail(ProcessMemoryBadAlias);
        if (IndexedLayoutPage(physical, /*byFrame=*/true) >= 0)
            return Fail(ProcessMemoryBadAlias);
    }
    return true;
}
bool ProcessAddressSpace::ValidateUserRange(uint32_t address, uint32_t length, bool writable) const {
    InterruptGuard guard;
    if (!prepared) return Fail(ProcessMemoryBadState);
    if (!Context(true)) return false;
    if (sealed && contentCopyLayoutValid) {
        // The root and directories still match the sealed supervisor
        // template. Mapping mutations run Layout(true) and invalidate this
        // private index before changing ownership or permissions.
        if (!LayoutRoot() || !LayoutDirectories(nullptr)) {
            contentCopyLayoutValid = false;
            return false;
        }
    } else if (!Layout(false)) {
        return false;
    }
    if (!Range(address, length, writable)) return false;
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
        uint8_t* output = (uint8_t*)(pages[IndexedLayoutPage(destination & AddressMask)].frame + offset);
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
        const uint8_t* input = (const uint8_t*)(pages[IndexedLayoutPage(source & AddressMask)].frame + offset);
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
    contentCopyLayoutValid = false;
    for (uint32_t i = 0; i < pageCount; ++i) {
        if (!SharesFrame(pages[i])) { ClearPage(pages[i].frame); frames->free(pages[i].frame); }
        pages[i].address = pages[i].frame = pages[i].region = 0;
        pages[i].writable = pages[i].present = false;
    }
    for (uint32_t i = 0; i < 1024; ++i) if (tables[i]) {
        ClearPage(tables[i]); frames->free(tables[i]); tables[i] = 0;
    }
    ClearPage(directory); frames->free(directory);
    if (metadataBase) {
        for (uint32_t i = 0; i < metadataPages; ++i) ClearPage(metadataBase + i * 4096);
        frames->freeContiguous(metadataBase, metadataPages);
        metadataBase = metadataPages = 0; pages = inlinePages; pageLimit = MaximumPages;
    }
    for (uint32_t i = 0; i < MaximumRegions; ++i)
        regions[i] = {};
    directory = kernelDirectory = pageCount = 0; dynamicLimit = DynamicLimit;
    kernel = 0; frames = 0; prepared = sealed = false;
    error = ProcessMemoryOk;
    return true;
}
uint32_t ProcessAddressSpace::DirectoryAddress() const { return directory; }
bool ProcessAddressSpace::QueryStatistics(ProcessMemoryStatistics& info) const {
    InterruptGuard guard;
    if (!prepared || !Context(true) || !Layout(false)) return false;
    ProcessMemoryStatistics result = {pageCount, 0, pageLimit, metadataPages, 0};
    for (uint32_t i = 0; i < pageCount; ++i) if (pages[i].region) ++result.dynamicPages;
    for (uint32_t di = 0; di < 1024; ++di) if (tables[di]) ++result.tableFrames;
    info = result; error = ProcessMemoryOk;
    return true;
}
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
        case ProcessMemoryRegionLimit: return "process region limit reached";
        case ProcessMemoryHandleExhausted: return "process region handles exhausted";
        default: return "unknown process memory error";
    }
}
