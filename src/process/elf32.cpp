#include <process/elf32.h>

namespace gtos { namespace process {
namespace {
    const uint32_t HeaderSize = 52, ProgramHeaderSize = 32, SectionHeaderSize = 40;
    const uint32_t PageMask = Elf32PageSize - 1;
    uint16_t Read16(const uint8_t* data) {
        return uint16_t(uint16_t(data[0]) | (uint16_t(data[1]) << 8));
    }
    uint32_t Read32(const uint8_t* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
               (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    }
    bool FileRange(uint32_t offset, uint32_t length, uint32_t imageSize) {
        return offset <= imageSize && length <= imageSize - offset;
    }
    void Clear(Elf32LoadPlan& plan) {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(&plan);
        for (uint32_t i = 0; i < sizeof(plan); ++i) bytes[i] = 0;
    }
    bool Fail(Elf32Error value, Elf32Error& error) {
        error = value;
        return false;
    }
    bool Alignment(uint32_t alignment, uint32_t address, uint32_t offset) {
        return alignment <= 1 ||
               ((alignment & (alignment - 1)) == 0 &&
                (address & (alignment - 1)) == (offset & (alignment - 1)));
    }
    bool ReadImage(void* source, uint32_t offset, void* buffer, uint32_t bytes) {
        const uint8_t* input = static_cast<const uint8_t*>(source) + offset;
        uint8_t* output = static_cast<uint8_t*>(buffer);
        for (uint32_t i = 0; i < bytes; ++i) output[i] = input[i];
        return true;
    }
    bool Sections(const uint8_t* header, Elf32ReadAt read, void* source,
                  uint32_t imageSize, Elf32Error& error) {
        uint32_t offset = Read32(header + 32);
        uint32_t count = Read16(header + 48), entrySize = Read16(header + 46);
        uint32_t strings = Read16(header + 50);
        if (!count) {
            // Extended numbering is deliberately unsupported; zero count means
            // the table is absent, not that section zero supplies another count.
            if (offset || strings || (entrySize != 0 && entrySize != SectionHeaderSize))
                return Fail(Elf32BadSectionTable, error);
            return true;
        }
        if (count > Elf32MaximumSectionHeaders) return Fail(Elf32SectionHeaderLimit, error);
        if (offset < HeaderSize || entrySize != SectionHeaderSize ||
            (strings && strings >= count) || !FileRange(offset, count * entrySize, imageSize))
            return Fail(Elf32BadSectionTable, error);
        uint32_t stringType = 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t section[SectionHeaderSize];
            if (!read(source, offset + i * entrySize, section, sizeof(section)))
                return Fail(Elf32ReadFailed, error);
            uint32_t type = Read32(section + 4), flags = Read32(section + 8);
            if (!i) {
                // Canonical null section, including no extended phnum/shnum.
                for (uint32_t byte = 0; byte < SectionHeaderSize; ++byte)
                    if (section[byte]) return Fail(Elf32BadSectionTable, error);
                continue;
            }
            if (type == 4 || type == 9 || type == 19 || // RELA, REL, RELR
                type == 0x60000001u || type == 0x60000002u || type == 0x6FFFFF00u)
                return Fail(Elf32UnsupportedRelocation, error); // Android packed relocations too.
            if (type == 6 || type == 11) return Fail(Elf32UnsupportedDynamic, error); // DYNAMIC/DYNSYM
            if (flags & 0x400u) return Fail(Elf32UnsupportedTls, error); // SHF_TLS
            if (i == strings) stringType = type;
            // NOBITS occupies no file bytes, even when sh_size describes BSS.
            if (type != 8 && !FileRange(Read32(section + 16), Read32(section + 20), imageSize))
                return Fail(Elf32BadFileRange, error);
        }
        if (strings && stringType != 3) return Fail(Elf32BadSectionTable, error);
        return true;
    }
}

static bool ValidateElf32WithLimit(Elf32ReadAt read, void* source, uint32_t imageSize,
                                  Elf32LoadPlan& output, Elf32Error& error, uint32_t pageLimit,
                                  uint32_t maximumPages) {
    Clear(output);
    error = Elf32Ok;
    if (!pageLimit || pageLimit > maximumPages) return Fail(Elf32PageLimit, error);
    if (!read) return Fail(Elf32NullImage, error);
    if (imageSize < HeaderSize) return Fail(Elf32TruncatedHeader, error);
    uint8_t image[HeaderSize];
    if (!read(source, 0, image, sizeof(image))) return Fail(Elf32ReadFailed, error);
    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F')
        return Fail(Elf32BadMagic, error);
    if (image[4] != 1) return Fail(Elf32UnsupportedClass, error);
    if (image[5] != 1) return Fail(Elf32UnsupportedEncoding, error);
    if (image[6] != 1 || Read32(image + 20) != 1) return Fail(Elf32UnsupportedVersion, error);
    if (image[7] || image[8]) return Fail(Elf32UnsupportedAbi, error); // System V ABI version zero.
    if (Read16(image + 16) != 2) return Fail(Elf32UnsupportedType, error); // ET_EXEC only.
    if (Read16(image + 18) != 3) return Fail(Elf32UnsupportedMachine, error); // EM_386 only.
    if (Read32(image + 36)) return Fail(Elf32UnsupportedFlags, error);
    if (Read16(image + 40) != HeaderSize) return Fail(Elf32BadHeaderSize, error);
    uint32_t phOffset = Read32(image + 28), phCount = Read16(image + 44);
    if (phCount > Elf32MaximumProgramHeaders) return Fail(Elf32ProgramHeaderLimit, error);
    if (!phCount || Read16(image + 42) != ProgramHeaderSize || phOffset < HeaderSize ||
        !FileRange(phOffset, phCount * ProgramHeaderSize, imageSize))
        return Fail(Elf32BadProgramTable, error);
    if (!Sections(image, read, source, imageSize, error)) return false;

    Elf32LoadPlan plan;
    Clear(plan);
    plan.entry = Read32(image + 24);
    bool executableEntry = false;
    for (uint32_t i = 0; i < phCount; ++i) {
        uint8_t header[ProgramHeaderSize];
        if (!read(source, phOffset + i * ProgramHeaderSize, header, sizeof(header)))
            return Fail(Elf32ReadFailed, error);
        uint32_t type = Read32(header);
        if (!type) continue; // PT_NULL fields have no meaning.
        if (type == 2) return Fail(Elf32UnsupportedDynamic, error);
        if (type == 3) return Fail(Elf32UnsupportedInterpreter, error);
        if (type == 7) return Fail(Elf32UnsupportedTls, error);
        if (type != 1 && type != 4 && type != 0x6474E551u)
            return Fail(Elf32UnsupportedProgramHeader, error);
        uint32_t offset = Read32(header + 4), address = Read32(header + 8);
        uint32_t fileSize = Read32(header + 16), memorySize = Read32(header + 20);
        uint32_t flags = Read32(header + 24), alignment = Read32(header + 28);
        if (!FileRange(offset, fileSize, imageSize)) return Fail(Elf32BadFileRange, error);
        if (type == 4) continue; // PT_NOTE is opaque, nonloaded informational data.
        if (type == 0x6474E551u) { // PT_GNU_STACK describes policy, not memory to load.
            if (flags & Elf32Execute) return Fail(Elf32ExecutableStack, error);
            if (flags != (Elf32Read | Elf32Write) || offset || address ||
                fileSize || memorySize || !Alignment(alignment, 0, 0))
                return Fail(Elf32BadStackHeader, error);
            continue;
        }
        if (!memorySize || fileSize > memorySize) return Fail(Elf32BadSegmentSize, error);
        if (!(flags & Elf32Read) || (flags & ~(Elf32Read | Elf32Write | Elf32Execute)))
            return Fail(Elf32BadSegmentFlags, error);
        if ((flags & (Elf32Write | Elf32Execute)) == (Elf32Write | Elf32Execute))
            return Fail(Elf32WritableExecutable, error);
        if (!Alignment(alignment, address, offset) ||
            (address & PageMask) != (offset & PageMask))
            return Fail(Elf32BadAlignment, error);
        if (address < Elf32UserBase || address >= Elf32UserLimit ||
            memorySize > Elf32UserLimit - address)
            return Fail(Elf32BadVirtualRange, error);
        if (plan.segmentCount == Elf32MaximumSegments) return Fail(Elf32SegmentLimit, error);
        // End and round-up cannot overflow: the user arena ends below UINT32_MAX.
        uint32_t startPage = address & ~PageMask;
        uint32_t endPage = (address + memorySize + PageMask) & ~PageMask;
        uint32_t pages = (endPage - startPage) / Elf32PageSize;
        if (pages > pageLimit - plan.pageCount) return Fail(Elf32PageLimit, error);
        for (uint32_t prior = 0; prior < plan.segmentCount; ++prior) {
            const Elf32LoadSegment& segment = plan.segments[prior];
            uint32_t priorStart = segment.virtualAddress & ~PageMask;
            uint32_t priorEnd = (segment.virtualAddress + segment.memorySize + PageMask) & ~PageMask;
            if (startPage < priorEnd && priorStart < endPage) return Fail(Elf32PageOverlap, error);
        }
        if ((flags & Elf32Execute) && plan.entry >= address && plan.entry - address < fileSize)
            executableEntry = true;
        Elf32LoadSegment& segment = plan.segments[plan.segmentCount++];
        segment.fileOffset = offset;
        segment.fileSize = fileSize;
        segment.memorySize = memorySize;
        segment.virtualAddress = address;
        segment.flags = flags;
        plan.pageCount += pages;
    }
    if (!plan.segmentCount) return Fail(Elf32NoLoadSegments, error);
    if (!executableEntry) return Fail(Elf32BadEntry, error);
    output = plan;
    return true;
}

bool ValidateElf32(const uint8_t* image, uint32_t imageSize,
                   Elf32LoadPlan& output, Elf32Error& error, uint32_t pageLimit) {
    return ValidateElf32WithLimit(image ? ReadImage : 0, const_cast<uint8_t*>(image),
                                  imageSize, output, error, pageLimit, Elf32MaximumLargePages);
}

bool ValidateContentElf32(const uint8_t* image, uint32_t imageSize,
                          Elf32LoadPlan& output, Elf32Error& error, uint32_t pageLimit) {
    return ValidateElf32WithLimit(image ? ReadImage : 0, const_cast<uint8_t*>(image),
                                  imageSize, output, error, pageLimit, Elf32MaximumContentPages);
}

bool ValidateContentElf32File(Elf32ReadAt read, void* source, uint32_t imageSize,
                              Elf32LoadPlan& output, Elf32Error& error, uint32_t pageLimit) {
    return ValidateElf32WithLimit(read, source, imageSize, output, error, pageLimit,
                                  Elf32MaximumContentFilePages);
}

bool ValidateElf32(const uint8_t* image, uint32_t imageSize, Elf32LoadPlan& output) {
    Elf32Error error;
    return ValidateElf32(image, imageSize, output, error);
}

bool ValidateElf32(const uint8_t* image, uint32_t imageSize,
                   Elf32LoadPlan& output, Elf32Error& error) {
    return ValidateElf32(image, imageSize, output, error, Elf32MaximumPages);
}

const char* Elf32ErrorName(Elf32Error error) {
    switch (error) {
    case Elf32Ok: return "ok";
    case Elf32NullImage: return "null image";
    case Elf32TruncatedHeader: return "truncated ELF header";
    case Elf32BadMagic: return "invalid ELF magic";
    case Elf32UnsupportedClass: return "requires ELF32";
    case Elf32UnsupportedEncoding: return "requires little-endian ELF";
    case Elf32UnsupportedVersion: return "unsupported ELF version";
    case Elf32UnsupportedAbi: return "unsupported ELF ABI";
    case Elf32UnsupportedType: return "requires fixed-address ET_EXEC";
    case Elf32UnsupportedMachine: return "requires EM_386";
    case Elf32UnsupportedFlags: return "unsupported processor flags";
    case Elf32BadHeaderSize: return "invalid ELF header size";
    case Elf32BadProgramTable: return "invalid program header table";
    case Elf32ProgramHeaderLimit: return "program header limit exceeded";
    case Elf32BadSectionTable: return "invalid section header table";
    case Elf32SectionHeaderLimit: return "section header limit exceeded";
    case Elf32BadFileRange: return "file range outside image";
    case Elf32UnsupportedDynamic: return "dynamic linking unsupported";
    case Elf32UnsupportedInterpreter: return "ELF interpreter unsupported";
    case Elf32UnsupportedTls: return "ELF TLS unsupported";
    case Elf32UnsupportedRelocation: return "ELF relocations unsupported";
    case Elf32UnsupportedProgramHeader: return "unsupported program header type";
    case Elf32ExecutableStack: return "executable stack rejected";
    case Elf32BadStackHeader: return "invalid stack policy header";
    case Elf32BadSegmentSize: return "invalid load segment size";
    case Elf32BadSegmentFlags: return "load segment must be readable with known flags";
    case Elf32WritableExecutable: return "writable executable segment rejected";
    case Elf32BadAlignment: return "invalid segment alignment or congruence";
    case Elf32BadVirtualRange: return "load segment outside user arena";
    case Elf32SegmentLimit: return "load segment limit exceeded";
    case Elf32PageLimit: return "image page limit exceeded";
    case Elf32PageOverlap: return "load segments share a page";
    case Elf32NoLoadSegments: return "no load segments";
    case Elf32BadEntry: return "entry is not file-backed executable code";
    case Elf32ReadFailed: return "could not read ELF headers";
    }
    return "unknown ELF error";
}
} }
