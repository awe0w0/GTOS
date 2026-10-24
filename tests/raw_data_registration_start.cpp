// GTOS-DSH-090 — the freestanding entry point and syscall layer for the unit test executable.
//
// This is the only file that talks to the host kernel. It provides:
//
//   _start        the process entry point the linker is told to use (-Wl,-e,_start)
//   test_write    write(1, buffer, length)   through int $0x80
//   test_exit     exit(code)                 through int $0x80, and never returns
//
// There is no C library here, no CRT, no heap and no standalone .s. The whole file is the two Linux i386
// system calls a test harness actually needs, written as inline assembly. `-nostdlib -static -no-pie` means
// nothing is linked in that could do this work instead, and nothing is downloaded or installed to make it work.
//
// WHY int $0x80 AND NOT libc
//
// The unit executable must be a static ELF with no PT_INTERP and no dynamic dependencies, because the ELF gate
// refuses anything that needs a loader this packet never granted. A static libc would satisfy that, but linking
// one would drag in far more than a test needs and would make "the test passed" depend on a libc's behaviour.
// Two system calls are enough: write to stdout, exit with a status.
//
// WHAT THIS DOES NOT DO
//
// It performs no IRQ work, touches no AP, no MSR, no page table and no hardware. It does not start a kernel and
// it is not part of one: this binary only ever runs as a host unit test process.

extern "C" int run_tests();

// write(1, text, length). The kernel preserves no caller-saved register across the trap, so the four operands
// are pinned into eax/ebx/ecx/edx and the result is discarded deliberately.
extern "C" void test_write(const char* text, unsigned int length) {
    if (text == 0 || length == 0) { return; }
    __asm__ __volatile__("int $0x80"
                         :
                         : "a"(4), "b"(1), "c"(text), "d"(length)
                         : "memory");
}

// exit(code). The loop is not defensive padding: a failed exit syscall must not let the process fall through
// into whatever happens to follow, which would turn a test failure into an undefined instruction.
extern "C" void test_exit(int code) {
    for (;;) {
        __asm__ __volatile__("int $0x80" : : "a"(1), "b"(code) : "memory");
    }
}

extern "C" void _start() {
    int status = run_tests();
    test_exit(status);
}
