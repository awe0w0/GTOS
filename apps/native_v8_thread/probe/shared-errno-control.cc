// Test-only wrong-storage control. Never part of the native runtime.
extern "C" int* __wrap___llvm_libc_errno() noexcept {
    static int shared;
    return &shared;
}
