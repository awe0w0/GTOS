#include <memory/paging.h>
#include <gdt.h>
using namespace gtos;
using namespace gtos::memory;
extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;
extern "C" void PagingFaultEntry(), PagingUnexpectedEntry();
namespace {
    uint32_t expectedAddress, expectedError;
    bool expectFault;
    const char protectedData[] = "immutable kernel rodata";
    struct Gate { uint16_t low, selector; uint8_t zero, access; uint16_t high; } __attribute__((packed));
    Gate gates[256];
    void Print(const char* text) { while (*text) { asm volatile("outb %0,$0xE9" : : "a"(*text)); ++text; } }
    void Hex(uint32_t value) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            char c = "0123456789ABCDEF"[(value >> shift) & 15];
            asm volatile("outb %0,$0xE9" : : "a"(c));
        }
    }
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        Print(pass ? "PAGING SMOKE PASS\n" : "PAGING SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xF4));
        for (;;) asm volatile("cli; hlt");
    }
    void Require(bool ok, const char* description) {
        if (!ok) { Print("FAILED "); Print(description); Print("\n"); Finish(false); }
    }
    bool Contains(const char* text, const char* word) {
        if (!text) return false;
        for (; *text; ++text) {
            uint32_t i = 0; while (word[i] && text[i] == word[i]) ++i;
            if (!word[i]) return true;
        }
        return false;
    }
    void Write(uint32_t address) {
        asm volatile("movl $0x12345678,(%0)" : : "r"(address) : "memory");
    }
    void Read(uint32_t address) {
        uint32_t value; asm volatile("movl (%1),%0" : "=r"(value) : "r"(address) : "memory");
    }
    void InstallIdt(uint16_t selector) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t address = (uint32_t)(i == 14 ? PagingFaultEntry : PagingUnexpectedEntry);
            gates[i].low = address; gates[i].high = address >> 16;
            gates[i].selector = selector; gates[i].zero = 0; gates[i].access = 0x8E;
        }
        struct Descriptor { uint16_t limit; uint32_t address; } __attribute__((packed));
        Descriptor idt = {sizeof(gates) - 1, (uint32_t)gates};
        asm volatile("lidt %0" : : "m"(idt));
    }
}
extern "C" void PagingFault(uint32_t error, uint32_t address) {
    Print("PAGE FAULT CR2 "); Hex(address); Print(" ERROR "); Hex(error); Print("\n");
    Finish(expectFault && address == expectedAddress && error == expectedError);
}
extern "C" void PagingUnexpected() { Print("UNEXPECTED EXCEPTION\n"); Finish(false); }
extern "C" void PagingSmoke(void* multiboot, uint32_t magic) {
    Print("PAGING SMOKE BOOT\n");
    GlobalDescriptorTable gdt;
    InstallIdt(gdt.CodeSegmentSelector());
    PhysicalMemoryManager frames;
    Require(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "physical initialize");
    KernelPaging paging;
    const MultibootInfo* info = (const MultibootInfo*)multiboot;
    const char* command = (info->flags & 4) ? (const char*)info->commandLine : 0;
    // Consume boot strings before PG: command-line pages are deliberately not
    // retained unless they happen to share an explicitly required page.
    bool sealTest = Contains(command, "case=seal");
    bool legacyFlagsTest = Contains(command, "case=legacyflags");
    bool paeTest = Contains(command, "case=pae");
    bool nullTest = Contains(command, "case=null");
    bool textTest = Contains(command, "case=text");
    bool rodataTest = Contains(command, "case=rodata");
    bool mmioTest = Contains(command, "case=mmio");
    bool unmapTest = Contains(command, "case=unmap");
    bool protectTest = Contains(command, "case=protect");
    PagingDeviceRange devices[] = {{0xA0000, 0x20000}, {0xFEE00000, 4096}};
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        info, devices, 2};
    Require(paging.prepareIdentity(frames, config), "prepare identity");
    PagingMapping mapping;
    Require(!paging.query(0, mapping), "null absent");
    Require(!paging.query(0xFEC00000, mapping), "unrequested IOAPIC absent");
    Require(paging.query((uint32_t)protectedData, mapping) && !mapping.writable, "rodata readonly");
    Require(paging.query((uint32_t)PagingSmoke, mapping) && !mapping.writable, "text readonly");
    if (paeTest) {
        uint32_t savedCr0, savedCr3, savedCr4;
        asm volatile("movl %%cr0,%0; movl %%cr3,%1; movl %%cr4,%2"
                     : "=r"(savedCr0), "=r"(savedCr3), "=r"(savedCr4));
        uint32_t pae = savedCr4 | (1u << 5);
        asm volatile("movl %0,%%cr4" : : "r"(pae) : "memory");
        Require(!paging.enable() && paging.getLastError() == PagingUnsafeContext, "reject PAE directory mode");
        uint32_t observedCr0, observedCr3, observedCr4;
        asm volatile("movl %%cr0,%0; movl %%cr3,%1; movl %%cr4,%2"
                     : "=r"(observedCr0), "=r"(observedCr3), "=r"(observedCr4));
        Require(observedCr0 == savedCr0 && observedCr3 == savedCr3 && observedCr4 == pae,
                "PAE rejection preserves CR0/CR3/CR4");
        asm volatile("movl %0,%%cr4" : : "r"(savedCr4) : "memory");
        Print("PAE ACTIVATION REJECTED SAFELY\n");
    }
    uint32_t sharedData = 0;
    if (sealTest) {
        Require(frames.allocate(sharedData), "shared data frame");
        Require(paging.mapOwnedPage(0xD0000000, sharedData, true), "shared alias prepared");
        Require(paging.sealForSharedProcessors(), "seal before sharing CR3");
        Require(paging.getStatistics().sealedForSharing, "shared status reported");
        Require(!paging.abandon() && paging.getLastError() == PagingSealed, "sealed teardown rejected before PG");
        Require(!paging.mapOwnedPage(0xD0001000, sharedData, true), "sealed map rejected before PG");
        Require(!paging.unmapOwnedPage(0xD0000000), "sealed unmap rejected before PG");
        Require(!paging.protectOwnedPage(0xD0000000, false), "sealed protect rejected before PG");
    }
    uint32_t expectedCr4 = 0;
    if (legacyFlagsTest) {
        asm volatile("movl %%cr4,%0" : "=r"(expectedCr4));
        expectedCr4 |= (1u << 4) | (1u << 7); // PSE and PGE, with PAE still clear.
        asm volatile("movl %0,%%cr4" : : "r"(expectedCr4) : "memory");
    }
    Require(paging.enable(), "enable paging");
    if (legacyFlagsTest) {
        uint32_t observed; asm volatile("movl %%cr4,%0" : "=r"(observed));
        Require(observed == expectedCr4, "preserve compatible PSE/PGE flags");
        Print("PSE/PGE LEGACY MAPPINGS PASS\n");
    }
    uint32_t cr0, cr3;
    asm volatile("movl %%cr0,%0; movl %%cr3,%1" : "=r"(cr0), "=r"(cr3));
    Require((cr0 & 0x80010000u) == 0x80010000u, "PG and WP set");
    Require(cr3 == paging.getStatistics().directoryAddress, "CR3 directory");
    Print("CR0 "); Hex(cr0); Print(" CR3 "); Hex(cr3); Print("\n");
    Require(info->flags != 0, "boot info readable");
    Require((info->flags & 8) && info->moduleCount == 1, "GRUB module retained");
    const MultibootModule* module = (const MultibootModule*)info->modules;
    Require(module->end > module->start && *(const uint8_t*)module->start == 'G', "module payload readable");
    *(volatile uint16_t*)0xB8000 = 0x0750;
    if (sealTest) {
        Write(0xD0000000);
        Require(*(volatile uint32_t*)sharedData == 0x12345678, "sealed shared alias usable");
        Require(!paging.abandon() && !paging.unmapOwnedPage(0xD0000000)
                && !paging.protectOwnedPage(0xD0000000, false), "sealed tables retained after PG");
        Print("SHARED MAPPING SEAL PASS\n");
        Finish(true);
    }
    uint32_t data; Require(frames.allocate(data), "allocate after paging");
    *(volatile uint32_t*)data = 0xAABBCCDD;
    const uint32_t alias = 0xC0000000;
    Require(paging.mapOwnedPage(alias, data, true), "new page table after paging");
    Require(*(volatile uint32_t*)alias == 0xAABBCCDD, "alias read");
    Write(alias);
    Require(*(volatile uint32_t*)data == 0x12345678, "alias write");
    if (nullTest || textTest || rodataTest || mmioTest || unmapTest || protectTest) {
        expectFault = true;
        expectedAddress = nullTest ? 0 : textTest ? (uint32_t)PagingSmoke : rodataTest
            ? (uint32_t)protectedData : mmioTest ? 0xFEC00000 : alias;
        expectedError = (textTest || rodataTest || protectTest) ? 3 : 0;
        if (unmapTest) Require(paging.unmapOwnedPage(alias), "unmap populated TLB entry");
        if (protectTest) Require(paging.protectOwnedPage(alias, false), "protect populated TLB entry");
        Print("EXPECT PAGE FAULT\n");
        if (expectedError == 3) Write(expectedAddress); else Read(expectedAddress);
        Finish(false);
    }
    Require(paging.unmapOwnedPage(alias), "unmap alias");
    Require(frames.free(data), "release alias frame");
    Require(!paging.abandon(), "active tables cannot be reclaimed");
    Finish(true);
}
