#ifndef __GTOS__MEMORY__PROCESS_ADDRESS_SPACE_H
#define __GTOS__MEMORY__PROCESS_ADDRESS_SPACE_H
#include <memory/paging.h>
namespace gtos { namespace memory {
    enum ProcessMemoryError : uint32_t {
        ProcessMemoryOk, ProcessMemoryBadState, ProcessMemoryBadRange,
        ProcessMemoryNoMemory, ProcessMemoryConflict, ProcessMemoryNotMapped,
        ProcessMemoryPermission, ProcessMemoryLimit, ProcessMemorySealed,
        ProcessMemoryActive, ProcessMemoryUnsafeContext, ProcessMemoryBadTemplate,
        ProcessMemoryBadMapping, ProcessMemoryBadAlias
    };
    // BSP-only, non-PAE private user mappings. KernelPaging must be enabled and
    // permanently sealed; it and its allocator must outlive every process.
    // No destructor tears mappings down. Before Destroy(), remove ALL runnable
    // references and deferred interrupt/return frames, then switch to kernel CR3.
    // Interrupt masking is not an SMP lock: APs must never enter this directory.
    class ProcessAddressSpace {
    public:
        static const uint32_t UserBase = 0x40000000u;
        static const uint32_t UserLimit = 0xC0000000u; // Exclusive.
        static const uint32_t MaximumPages = 256;
    private:
        struct Page { uint32_t address, frame; bool writable; };
        KernelPaging* kernel;
        PhysicalMemoryManager* frames;
        uint32_t directory, kernelDirectory, pageCount;
        uint32_t tables[1024]; // Owned tables only; borrowed tables stay zero.
        Page pages[MaximumPages]; // Authoritative ownership, never inferred from PTEs.
        bool prepared, sealed;
        mutable ProcessMemoryError error;
        bool Fail(ProcessMemoryError value) const;
        bool Context(bool allowOwnDirectory) const;
        bool KernelAlias(uint32_t address, uint32_t length, bool writable) const;
        bool Layout(bool complete) const;
        bool Range(uint32_t address, uint32_t length, bool writable) const;
        bool Buffer(uint32_t address, uint32_t length, bool writable) const;
        bool Mutable() const;
        int FindPage(uint32_t address) const;
        bool AllocateFrame(uint32_t& address);
        ProcessAddressSpace(const ProcessAddressSpace&);
        ProcessAddressSpace& operator=(const ProcessAddressSpace&);
    public:
        ProcessAddressSpace();
        // Requires kernel CR3 active on BSP. Transactional, including overlap
        // rejection for ANY present kernel PDE in the entire fixed user arena.
        // The allocator must be the identical instance used by sharedKernel.
        bool Prepare(KernelPaging& sharedKernel, PhysicalMemoryManager& allocator);
        // New zeroed data frames, never caller-supplied physical aliases.
        // Mapping/protection changes require unsealed state and kernel CR3.
        bool MapNewPage(uint32_t virtualAddress, bool writable);
        bool ProtectPage(uint32_t virtualAddress, bool writable);
        bool UnmapPage(uint32_t virtualAddress);
        // Freeze mappings before exposing the CR3 to a runnable task. Idempotent.
        bool Seal();
        // Full-range validation precedes all copying. Kernel buffers must be
        // mapped trusted supervisor buffers, disjoint from all owned
        // frames and all paging structures. A valid zero-length user address
        // remains inside [UserBase,UserLimit), even when no page is mapped.
        bool ValidateUserRange(uint32_t address, uint32_t length, bool writable) const;
        bool CopyToUser(uint32_t destination, const void* source, uint32_t length);
        bool CopyFromUser(void* destination, uint32_t source, uint32_t length) const;
        uint32_t DirectoryAddress() const;
        bool Prepared() const;
        bool Sealed() const;
        // Explicit deferred reap only. Never frees borrowed kernel frames;
        // refuses active CR3 or inconsistent tables without partial cleanup.
        bool Destroy();
        ProcessMemoryError GetLastError() const;
        static const char* ErrorName(ProcessMemoryError value);
    };
} }
#endif
