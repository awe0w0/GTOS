#include <process/elf32.h>
#include "elf32_fixture.h"
using namespace gtos::process;
#ifdef ELF32_SANITIZED
extern "C" void __asan_poison_memory_region(void const volatile*, unsigned long);
extern "C" void __asan_unpoison_memory_region(void const volatile*, unsigned long);
#endif
namespace {
    const uint32_t Capacity = 16384, ImageSize = 12288, Payload = 4096, SectionTable = 8192;
    uint8_t buffer[Capacity];
    uint32_t checks = 0, failures = 0;
    void print(const char* text) {
        uint32_t length = 0; while (text[length]) ++length;
#ifdef __i386__
        uint32_t result;
        asm volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(text), "d"(length) : "memory", "cc");
#else
        long result;
        asm volatile("syscall" : "=a"(result) : "0"(1L), "D"(1L), "S"(text), "d"((unsigned long)length)
                     : "rcx", "r11", "memory", "cc");
#endif
    }
    void number(uint32_t value) {
        char digits[11]; uint32_t i = 10; digits[i] = 0;
        do { digits[--i] = '0' + value % 10; value /= 10; } while (value);
        print(digits + i);
    }
    void check(bool value, uint32_t line) {
        ++checks;
        if (!value) { ++failures; print("FAIL line "); number(line); print("\n"); }
    }
#define CHECK(value) check((value), __LINE__)
    void fill(void* destination, uint32_t bytes, uint8_t value) {
        uint8_t* data = static_cast<uint8_t*>(destination);
        for (uint32_t i = 0; i < bytes; ++i) data[i] = value;
    }
    bool equal(const void* a, const void* b, uint32_t bytes) {
        const uint8_t* x = static_cast<const uint8_t*>(a);
        const uint8_t* y = static_cast<const uint8_t*>(b);
        for (uint32_t i = 0; i < bytes; ++i) if (x[i] != y[i]) return false;
        return true;
    }
    bool textEqual(const char* a, const char* b) {
        if (!a || !b) return false;
        while (*a && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
    bool zero(const void* object, uint32_t bytes) {
        const uint8_t* data = static_cast<const uint8_t*>(object);
        for (uint32_t i = 0; i < bytes; ++i) if (data[i]) return false;
        return true;
    }
    void put16(uint32_t offset, uint16_t value) {
        buffer[offset] = uint8_t(value); buffer[offset + 1] = uint8_t(value >> 8);
    }
    void put32(uint32_t offset, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) buffer[offset + i] = uint8_t(value >> (i * 8));
    }
    void load(uint32_t index, uint32_t address = Elf32UserBase, uint32_t offset = Payload,
              uint32_t fileSize = 16, uint32_t memorySize = 4096, uint32_t flags = 5,
              uint32_t alignment = 4096) {
        uint32_t h = 52 + index * 32;
        put32(h, 1); put32(h + 4, offset); put32(h + 8, address);
        put32(h + 12, 0xFFFFFFFF); // Physical address is irrelevant to a user loader.
        put32(h + 16, fileSize); put32(h + 20, memorySize);
        put32(h + 24, flags); put32(h + 28, alignment);
    }
    void build(uint32_t count = 1) {
        fill(buffer, Capacity, 0);
        buffer[0] = 0x7F; buffer[1] = 'E'; buffer[2] = 'L'; buffer[3] = 'F';
        buffer[4] = buffer[5] = buffer[6] = 1;
        put16(16, 2); put16(18, 3); put32(20, 1); put32(24, Elf32UserBase);
        put32(28, 52); put16(40, 52); put16(42, 32); put16(44, uint16_t(count));
        for (uint32_t i = 0; i < count; ++i) load(i, Elf32UserBase + i * 4096);
        for (uint32_t i = 0; i < 16; ++i) buffer[Payload + i] = uint8_t(i);
    }
    void sections(uint32_t type = 1, uint32_t flags = 0) {
        put32(32, SectionTable); put16(46, 40); put16(48, 2);
        put32(SectionTable + 40 + 4, type); put32(SectionTable + 40 + 8, flags);
        put32(SectionTable + 40 + 16, Payload); put32(SectionTable + 40 + 20, 16);
    }
    void poison(uint32_t size) {
#ifdef ELF32_SANITIZED
        __asan_poison_memory_region(buffer + size, Capacity - size);
#else
        (void)size;
#endif
    }
    void unpoison() {
#ifdef ELF32_SANITIZED
        __asan_unpoison_memory_region(buffer, Capacity);
#endif
    }
    void expect(Elf32Error expected, uint32_t size = ImageSize) {
        Elf32LoadPlan plan; fill(&plan, sizeof(plan), 0xA5);
        Elf32Error error = Elf32Ok;
        poison(size);
        bool valid = ValidateElf32(buffer, size, plan, error);
        unpoison();
        if (error != expected) {
            print("expected "); print(Elf32ErrorName(expected)); print(", got ");
            print(Elf32ErrorName(error)); print("\n");
        }
        CHECK(valid == (expected == Elf32Ok)); CHECK(error == expected);
        if (!valid) CHECK(zero(&plan, sizeof(plan)));
    }
    Elf32LoadPlan good(uint32_t size = ImageSize) {
        Elf32LoadPlan plan; Elf32Error error;
        poison(size);
        bool valid = ValidateElf32(buffer, size, plan, error);
        unpoison();
        CHECK(valid); CHECK(error == Elf32Ok);
        return plan;
    }
    void headers() {
        Elf32LoadPlan plan; Elf32Error error;
        fill(&plan, sizeof(plan), 0xA5);
        CHECK(!ValidateElf32(0, 100, plan, error)); CHECK(error == Elf32NullImage);
        CHECK(zero(&plan, sizeof(plan)));
        build();
        for (uint32_t size = 0; size < 52; ++size) expect(Elf32TruncatedHeader, size);
        expect(Elf32BadProgramTable, 52); expect(Elf32BadProgramTable, 83);
        expect(Elf32BadFileRange, 84);
        build(); buffer[0] = 0; expect(Elf32BadMagic);
        build(); buffer[4] = 2; expect(Elf32UnsupportedClass);
        build(); buffer[5] = 2; expect(Elf32UnsupportedEncoding);
        build(); buffer[6] = 0; expect(Elf32UnsupportedVersion);
        build(); put32(20, 0); expect(Elf32UnsupportedVersion);
        build(); buffer[7] = 3; expect(Elf32UnsupportedAbi);
        build(); buffer[8] = 1; expect(Elf32UnsupportedAbi);
        for (uint32_t type = 0; type < 5; ++type) {
            if (type == 2) continue;
            build(); put16(16, uint16_t(type)); expect(Elf32UnsupportedType);
        }
        build(); put16(18, 62); expect(Elf32UnsupportedMachine);
        build(); put32(36, 1); expect(Elf32UnsupportedFlags);
        build(); put16(40, 51); expect(Elf32BadHeaderSize);
        build(); put16(42, 31); expect(Elf32BadProgramTable);
        build(); put16(44, 0); expect(Elf32BadProgramTable);
        build(); put16(44, 65); expect(Elf32ProgramHeaderLimit);
        build(); put16(44, 0xFFFF); expect(Elf32ProgramHeaderLimit);
        build(); put32(28, 0); expect(Elf32BadProgramTable);
        build(); put32(28, 0xFFFFFFF0); expect(Elf32BadProgramTable);
        build(); put32(28, ImageSize - 31); expect(Elf32BadProgramTable);
        build(); put16(44, 64); good(); // Sixty-three bounded PT_NULL entries.
        CHECK(textEqual(Elf32ErrorName(static_cast<Elf32Error>(0x7FFF)), "unknown ELF error"));
        CHECK(textEqual(Elf32ErrorName(static_cast<Elf32Error>(0xFFFFFFFFu)), "unknown ELF error"));
        for (uint32_t i = Elf32Ok; i <= Elf32BadEntry; ++i) {
            const char* name = Elf32ErrorName(static_cast<Elf32Error>(i));
            CHECK(name && name[0] && !(name[0] == 'u' && name[1] == 'n' && name[2] == 'k'));
        }
    }
    void segments() {
        build(); Elf32LoadPlan plan = good();
        CHECK(plan.entry == Elf32UserBase && plan.segmentCount == 1 && plan.pageCount == 1);
        CHECK(plan.segments[0].fileOffset == Payload && plan.segments[0].fileSize == 16);
        CHECK(plan.segments[0].memorySize == 4096 && plan.segments[0].virtualAddress == Elf32UserBase);
        CHECK(plan.segments[0].flags == 5);
        CHECK(zero(&plan.segments[1], sizeof(plan.segments) - sizeof(plan.segments[0])));
        CHECK(ValidateElf32(buffer, ImageSize, plan));
        buffer[0] = 0; CHECK(!ValidateElf32(buffer, ImageSize, plan)); CHECK(zero(&plan, sizeof(plan)));
        build(); put32(56, 0xFFFFFFF8); expect(Elf32BadFileRange);
        build(); put32(68, 0xFFFFFFFF); expect(Elf32BadFileRange);
        build(); put32(56, ImageSize); expect(Elf32BadFileRange);
        build(); put32(72, 15); expect(Elf32BadSegmentSize);
        build(); put32(68, 0); put32(72, 0); expect(Elf32BadSegmentSize);
        build(); put32(76, 1); expect(Elf32BadSegmentFlags);
        build(); put32(76, 0x80000005); expect(Elf32BadSegmentFlags);
        build(); put32(76, 7); expect(Elf32WritableExecutable);
        build(); put32(80, 3); expect(Elf32BadAlignment);
        build(); put32(60, Elf32UserBase + 1); expect(Elf32BadAlignment);
        build(); put32(80, 0); good();
        build(); put32(80, 1); good();
        build(); put32(80, 0); put32(60, Elf32UserBase + 1); expect(Elf32BadAlignment);
        build(); put32(80, 1); put32(60, Elf32UserBase + 1); expect(Elf32BadAlignment);
        build(); put32(80, 0x80000000); expect(Elf32BadAlignment);
        build(); load(0, 0x80001000, Payload, 16, 4096, 5, 0x80000000);
        put32(24, 0x80001000); good();
        build(); put32(60, Elf32UserBase - 4096); expect(Elf32BadVirtualRange);
        build(); put32(60, Elf32UserLimit); expect(Elf32BadVirtualRange);
        build(); put32(60, 0xFFFFF000); put32(72, 8192); expect(Elf32BadVirtualRange);
        build(); put32(72, 0xFFFFFFFF); expect(Elf32BadVirtualRange);
        build(); put32(60, Elf32UserLimit - 4096); put32(72, 4097); expect(Elf32BadVirtualRange);
        build(); load(0, Elf32UserLimit - 4096); put32(24, Elf32UserLimit - 1);
        put32(68, 4096); good(); // Last byte in arena, file backed.
        build(); put32(72, 256 * 4096); plan = good(); CHECK(plan.pageCount == 256);
        build(); put32(72, 256 * 4096 + 1); expect(Elf32PageLimit);
        build(2); put32(72, 256 * 4096); load(1, Elf32UserBase + 256 * 4096);
        expect(Elf32PageLimit);
        build(16); plan = good(); CHECK(plan.segmentCount == 16 && plan.pageCount == 16);
        build(17); expect(Elf32SegmentLimit);
        build(2); load(1, Elf32UserBase); expect(Elf32PageOverlap);
        build(2); load(0, Elf32UserBase, Payload, 16, 16, 5, 1);
        load(1, Elf32UserBase + 32, Payload + 32, 16, 16, 4, 1); expect(Elf32PageOverlap);
        build(2); load(0, Elf32UserBase + 1, Payload + 1, 16, 4096, 5, 4096);
        put32(24, Elf32UserBase + 1); expect(Elf32PageOverlap);
        build(2); load(0, Elf32UserBase + 4096); load(1, Elf32UserBase); good(); // Unsorted loads.
        build(); load(0, Elf32UserBase + 1, Payload + 1, 16, 4096); put32(24, Elf32UserBase + 1);
        plan = good(); CHECK(plan.pageCount == 2); // Counts partial first/last pages.
        build(2); load(1, Elf32UserBase + 4096, ImageSize, 0, 1, 6, 1);
        plan = good(); CHECK(plan.pageCount == 2); // Pure BSS is valid away from entry.
        build(); put32(68, 0); expect(Elf32BadEntry);
        build(); put32(24, Elf32UserBase + 16); expect(Elf32BadEntry); // First zero-filled byte.
        build(); put32(24, Elf32UserBase - 1); expect(Elf32BadEntry);
        build(); put32(24, Elf32UserBase + 4096); expect(Elf32BadEntry);
        build(); put32(76, 4); expect(Elf32BadEntry);
        build(); put32(76, 6); expect(Elf32BadEntry);
        build(); put32(52, 0); expect(Elf32NoLoadSegments);
    }
    void auxiliaryHeaders() {
        const uint32_t types[] = {2, 3, 7, 5, 6, 0x6474E552, 0x6474E553, 0xFFFFFFFF};
        const Elf32Error errors[] = {Elf32UnsupportedDynamic, Elf32UnsupportedInterpreter,
            Elf32UnsupportedTls, Elf32UnsupportedProgramHeader, Elf32UnsupportedProgramHeader,
            Elf32UnsupportedProgramHeader, Elf32UnsupportedProgramHeader, Elf32UnsupportedProgramHeader};
        for (uint32_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            build(2); put32(84, types[i]); expect(errors[i]);
        }
        build(2); fill(buffer + 84, 32, 0xFF); put32(84, 0); good(); // PT_NULL is ignored.
        build(2); put32(84, 4); good(); // Bounded opaque note.
        put32(88, 0xFFFFFFFF); expect(Elf32BadFileRange);
        build(2); fill(buffer + 84, 32, 0); put32(84, 0x6474E551); put32(108, 6); put32(112, 16);
        good(); put32(108, 7); expect(Elf32ExecutableStack);
        put32(108, 4); expect(Elf32BadStackHeader);
        put32(108, 6); put32(104, 4096); expect(Elf32BadStackHeader);
        put32(104, 0); put32(112, 3); expect(Elf32BadStackHeader);
        put32(112, 16); put32(92, Elf32UserBase); expect(Elf32BadStackHeader);
    }
    void sectionHeaders() {
        build(); sections(); good();
        build(); fill(buffer + 2048, 256 * 40, 0);
        put32(32, 2048); put16(46, 40); put16(48, 256); good();
        build(); put16(46, 40); good(); // Absent table may retain the canonical entry size.
        build(); put32(32, SectionTable); expect(Elf32BadSectionTable);
        build(); put16(50, 1); expect(Elf32BadSectionTable);
        build(); put16(46, 39); expect(Elf32BadSectionTable);
        build(); sections(); put16(46, 39); expect(Elf32BadSectionTable);
        build(); sections(); put16(48, 257); expect(Elf32SectionHeaderLimit);
        build(); sections(); put32(32, 0xFFFFFFF0); expect(Elf32BadSectionTable);
        build(); sections(); put32(32, ImageSize - 79); expect(Elf32BadSectionTable);
        build(); sections(); put32(32, 12); expect(Elf32BadSectionTable);
        build(); sections(); put16(50, 2); expect(Elf32BadSectionTable);
        build(); sections(); put16(50, 0xFFFF); expect(Elf32BadSectionTable);
        build(); sections(); put16(50, 1); expect(Elf32BadSectionTable);
        build(); sections(3); put16(50, 1); good();
        build(); sections(); put32(SectionTable + 20, 3); expect(Elf32BadSectionTable);
        build(); sections(); put32(SectionTable + 40 + 16, 0xFFFFFFF0); expect(Elf32BadFileRange);
        build(); sections(); put32(SectionTable + 40 + 20, 0xFFFFFFFF); expect(Elf32BadFileRange);
        build(); sections(8); put32(SectionTable + 40 + 16, 0xFFFFFFFF);
        put32(SectionTable + 40 + 20, 0xFFFFFFFF); good(); // NOBITS is never read from the file.
        build(); sections(4); expect(Elf32UnsupportedRelocation);
        build(); sections(9); expect(Elf32UnsupportedRelocation);
        build(); sections(19); expect(Elf32UnsupportedRelocation);
        build(); sections(0x60000001); expect(Elf32UnsupportedRelocation);
        build(); sections(0x60000002); expect(Elf32UnsupportedRelocation);
        build(); sections(0x6FFFFF00); expect(Elf32UnsupportedRelocation);
        build(); sections(6); expect(Elf32UnsupportedDynamic);
        build(); sections(11); expect(Elf32UnsupportedDynamic);
        build(); sections(1, 0x400); expect(Elf32UnsupportedTls);
    }
    uint32_t random(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
    }
    void invariant(const Elf32LoadPlan& plan, uint32_t size) {
        CHECK(plan.segmentCount > 0 && plan.segmentCount <= Elf32MaximumSegments);
        CHECK(plan.pageCount > 0 && plan.pageCount <= Elf32MaximumPages);
        uint32_t pages = 0; bool entry = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& s = plan.segments[i];
            CHECK(s.fileOffset <= size && s.fileSize <= size - s.fileOffset);
            CHECK(s.fileSize <= s.memorySize && s.memorySize > 0);
            CHECK(s.virtualAddress >= Elf32UserBase && s.virtualAddress < Elf32UserLimit);
            CHECK(s.memorySize <= Elf32UserLimit - s.virtualAddress);
            CHECK((s.flags & Elf32Read) && !(s.flags & ~7u) && (s.flags & 3u) != 3u);
            uint32_t first = s.virtualAddress / 4096;
            uint32_t last = (s.virtualAddress + s.memorySize - 1) / 4096;
            pages += last - first + 1;
            for (uint32_t j = 0; j < i; ++j) {
                const Elf32LoadSegment& p = plan.segments[j];
                uint32_t pf = p.virtualAddress / 4096;
                uint32_t pl = (p.virtualAddress + p.memorySize - 1) / 4096;
                CHECK(first > pl || pf > last);
            }
            if ((s.flags & Elf32Execute) && plan.entry >= s.virtualAddress &&
                plan.entry - s.virtualAddress < s.fileSize) entry = true;
        }
        CHECK(pages == plan.pageCount && entry);
        CHECK(zero(plan.segments + plan.segmentCount,
                   (Elf32MaximumSegments - plan.segmentCount) * sizeof(plan.segments[0])));
    }
    void fuzz() {
        uint32_t state = 0xE132CAFE;
        for (uint32_t iteration = 0; iteration < 20000; ++iteration) {
            build(1 + (random(state) % 3));
            if (iteration & 1) sections();
            uint32_t changes = 1 + random(state) % 12;
            for (uint32_t i = 0; i < changes; ++i) {
                uint32_t offset = random(state) % 4 == 0 ? SectionTable + random(state) % 80 : random(state) % 160;
                buffer[offset] = uint8_t(random(state));
            }
            uint32_t size = iteration % 3 ? ImageSize : random(state) % ImageSize;
            Elf32LoadPlan a, b; fill(&a, sizeof(a), 0xA5); fill(&b, sizeof(b), 0x5A);
            Elf32Error ae, be;
            poison(size);
            bool av = ValidateElf32(buffer, size, a, ae);
            bool bv = ValidateElf32(buffer, size, b, be);
            unpoison();
            CHECK(av == bv && ae == be && equal(&a, &b, sizeof(a)));
            if (av) { CHECK(ae == Elf32Ok); invariant(a, size); }
            else { CHECK(ae != Elf32Ok); CHECK(zero(&a, sizeof(a))); }
        }
        for (uint32_t iteration = 0; iteration < 1000; ++iteration) {
            uint32_t size = random(state) % 512;
            for (uint32_t i = 0; i < size; ++i) buffer[i] = uint8_t(random(state));
            Elf32LoadPlan plan; Elf32Error error;
            poison(size);
            bool accepted = ValidateElf32(buffer, size, plan, error);
            unpoison();
            if (accepted) invariant(plan, size); else CHECK(zero(&plan, sizeof(plan)));
        }
    }
    void realFixture() {
        Elf32LoadPlan plan; Elf32Error error;
        CHECK(ValidateElf32(elf32Fixture, sizeof(elf32Fixture), plan, error));
        CHECK(error == Elf32Ok && plan.segmentCount == 2 && plan.pageCount == 4);
        CHECK(plan.entry == Elf32UserBase);
        CHECK(plan.segments[0].flags == 5 && plan.segments[1].flags == 6);
        CHECK(plan.segments[1].memorySize > plan.segments[1].fileSize);
        invariant(plan, sizeof(elf32Fixture));
        CHECK(sizeof(elf32Fixture) < Capacity);
        for (uint32_t i = 0; i < sizeof(elf32Fixture); ++i) buffer[i] = elf32Fixture[i];
        // Every byte-level truncation, with ASan poisoning the inaccessible tail.
        for (uint32_t size = 0; size < sizeof(elf32Fixture); ++size) {
            poison(size);
            bool valid = ValidateElf32(buffer, size, plan, error);
            unpoison();
            CHECK(!valid && error != Elf32Ok && zero(&plan, sizeof(plan)));
        }
        // Deliberately unaligned host input is parsed bytewise.
        for (uint32_t i = sizeof(elf32Fixture); i; --i) buffer[i] = buffer[i - 1];
        CHECK(ValidateElf32(buffer + 1, sizeof(elf32Fixture), plan, error));
    }
}
extern "C" int Elf32TestMain() {
    headers(); segments(); auxiliaryHeaders(); sectionHeaders(); fuzz(); realFixture();
    print("ELF32 tests: "); number(checks); print(" checks, "); number(failures); print(" failures\n");
    return failures ? 1 : 0;
}
#ifdef ELF32_HOSTED
int main() { return Elf32TestMain(); }
#else
// Kernel-provided entry stack has no return address: realign before a C++ call.
#ifdef __i386__
asm(".global _start\n_start:\n andl $-16, %esp\n call Elf32TestMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
#else
asm(".global _start\n_start:\n andq $-16, %rsp\n call Elf32TestMain\n movl %eax, %edi\n movl $60, %eax\n syscall\n");
#endif
#endif
