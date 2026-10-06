#ifndef __GTOS__PROCESS__ELF32_H
#define __GTOS__PROCESS__ELF32_H
#include <common/types.h>

namespace gtos { namespace process {
    // Validation limits, not hardware execute permissions (non-PAE has no NX).
    static const uint32_t Elf32UserBase = 0x40000000u;
    static const uint32_t Elf32UserLimit = 0xC0000000u; // Exclusive.
    static const uint32_t Elf32PageSize = 4096;
    static const uint32_t Elf32MaximumSegments = 16;
    static const uint32_t Elf32MaximumPages = 256;
    static const uint32_t Elf32MaximumProgramHeaders = 64;
    static const uint32_t Elf32MaximumSectionHeaders = 256;
    enum Elf32SegmentFlags { Elf32Execute = 1, Elf32Write = 2, Elf32Read = 4 };
    enum Elf32Error : uint32_t {
        Elf32Ok, Elf32NullImage, Elf32TruncatedHeader, Elf32BadMagic,
        Elf32UnsupportedClass, Elf32UnsupportedEncoding, Elf32UnsupportedVersion,
        Elf32UnsupportedAbi, Elf32UnsupportedType, Elf32UnsupportedMachine,
        Elf32UnsupportedFlags, Elf32BadHeaderSize, Elf32BadProgramTable,
        Elf32ProgramHeaderLimit, Elf32BadSectionTable, Elf32SectionHeaderLimit,
        Elf32BadFileRange, Elf32UnsupportedDynamic, Elf32UnsupportedInterpreter,
        Elf32UnsupportedTls, Elf32UnsupportedRelocation, Elf32UnsupportedProgramHeader,
        Elf32ExecutableStack, Elf32BadStackHeader, Elf32BadSegmentSize,
        Elf32BadSegmentFlags, Elf32WritableExecutable, Elf32BadAlignment,
        Elf32BadVirtualRange, Elf32SegmentLimit, Elf32PageLimit,
        Elf32PageOverlap, Elf32NoLoadSegments, Elf32BadEntry
    };
    struct Elf32LoadSegment {
        uint32_t fileOffset, fileSize, memorySize, virtualAddress, flags;
    };
    struct Elf32LoadPlan {
        uint32_t entry, segmentCount, pageCount;
        Elf32LoadSegment segments[Elf32MaximumSegments];
    };
    // image must denote imageSize readable, stable bytes in trusted kernel memory;
    // it must not alias output/error. This validates content, NOT the C++ pointer.
    // No allocation, mapping, user-address dereference, or global error state.
    // All output bytes are cleared on failure; unused segments are zero on success.
    // Image pages exclude stack/guard/runtime pages: the caller budgets those too.
    bool ValidateElf32(const uint8_t* image, uint32_t imageSize, Elf32LoadPlan& output);
    bool ValidateElf32(const uint8_t* image, uint32_t imageSize,
                       Elf32LoadPlan& output, Elf32Error& error);
    const char* Elf32ErrorName(Elf32Error error);
} }
#endif
