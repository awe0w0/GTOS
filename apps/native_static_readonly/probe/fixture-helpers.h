#include <process/native_runtime.h>
#include <process/elf32.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include "tests/native_process_probe_expectations.h"
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;
extern "C" uint8_t native_user_start, native_user_end;
void printf(char* text) { while (*text) { asm volatile("outb %0,$0xe9" : : "a"(*text)); ++text; } }
void printfHex32(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        char c = "0123456789ABCDEF"[(value >> shift) & 15];
        asm volatile("outb %0,$0xe9" : : "a"(c));
    }
}
void printfHex(uint8_t value) { printfHex32(value); }
void operator delete(void*) {}
namespace {
    volatile uint32_t ring0Progress;
    void Put32(uint8_t* p, uint32_t value) {
        for (uint32_t i = 0; i < 4; ++i) p[i] = value >> (i * 8);
    }
    uint32_t Get32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    void Ring0Task() { for (;;) ++ring0Progress; }
    void WaitTicks(TaskManager& tasks, uint32_t ticks) {
        uint32_t begin = tasks.Ticks();
        while ((uint32_t)(tasks.Ticks() - begin) < ticks) asm volatile("sti; hlt" : : : "memory");
    }
}
