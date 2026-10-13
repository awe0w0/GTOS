#include <process/abi.h>
#include <process/thread_abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>
#include <process/file_abi.h>
#include "record.h"
extern "C" {
volatile ThreadRecord native_thread_record __attribute__((section(".data.thread_record"))) = {1};
}
namespace {
int Call(unsigned op, unsigned a = 0, unsigned b = 0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "cc", "memory");
    return (int)op;
}
template <class T> int Request(unsigned op, T& r) { return Call(op, (unsigned)&r, sizeof(r)); }
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass, unsigned line) {
    asm volatile("lock incl %0" : "+m"(native_thread_record.checks) : : "cc", "memory");
    if (!pass) {
        native_thread_record.error = line;
        Call(GTOS_SYS_EXIT, line);
        for (;;) Yield();
    }
}
#define NEED(x) Need((x), __LINE__)
unsigned Mxcsr() { unsigned value; asm volatile("stmxcsr %0" : "=m"(value)); return value; }
void SetMxcsr(unsigned value) { asm volatile("ldmxcsr %0" : : "m"(value)); }
GtosProcessInfoResult Info() {
    GtosProcessInfoResult result = {};
    GtosProcessInfoRequest r = {1, 0, (unsigned)&result, sizeof(result)};
    NEED(Request(GTOS_SYS_PROCESS_INFO, r) == 0);
    return result;
}
int Join(unsigned id, unsigned& code) {
    GtosThreadJoinRequest r = {1, id, (unsigned)&code};
    return Request(GTOS_SYS_THREAD_TRY_JOIN, r);
}
void Reserve(unsigned index) {
    GtosVmReserveResult result = {};
    GtosVmReserveRequest r = {1, 16384, 4096, 0, (unsigned)&result};
    NEED(Request(GTOS_SYS_VM_RESERVE, r) == 0 && result.length == 16384);
    native_thread_record.worker[index].handle = result.handle;
    native_thread_record.worker[index].base = result.base;
    GtosVmRangeRequest rw = {1, result.handle, 4096, 8192, GTOS_VM_READ_WRITE};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, rw) == 0);
}
void Release(unsigned index) {
    GtosVmControlRequest r = {1, native_thread_record.worker[index].handle, 0};
    NEED(Request(GTOS_SYS_VM_RELEASE, r) == 0);
}
void Worker(unsigned index) {
    NEED(index < 3);
    volatile WorkerRecord& record = native_thread_record.worker[index];
    const GtosProcessInfoResult info = Info();
    record.initialMxcsr = Mxcsr();
    NEED(info.process_id == native_thread_record.parent && info.thread_id == record.id
        && info.thread_id != info.process_id && info.stack_begin == record.base + 4096
        && info.stack_end == record.base + 12288 && info.maximum_parallel_threads == 4
        && info.scheduler_cpu_count == 1 && record.initialMxcsr == 0x1f80);
    unsigned output = 0xaaaaaaaa;
    NEED(Join(info.thread_id, output) == GTOS_THREAD_ERR_DEADLOCK && output == 0xaaaaaaaa);
    NEED(Join(info.process_id, output) == GTOS_THREAD_ERR_NO_THREAD && output == 0xaaaaaaaa);
    SetMxcsr(0x3f80 + index * 0x2000);
    if (index == 0) {
        const char byte = 'T';
        GtosFileTransferRequest write = {1, native_thread_record.file, (unsigned)&byte, 1};
        NEED(Request(GTOS_SYS_FILE_WRITE, write) == 1);
    }
    record.stage = 1;
    while (!record.go) { ++record.loops; Yield(); }
    record.finalMxcsr = Mxcsr();
    NEED(record.finalMxcsr == 0x3f80 + index * 0x2000);
    record.stage = 2;
    if (native_thread_record.mode == 2 && index == 0)
        *(volatile unsigned*)0x100000 = 42;
    Call(GTOS_SYS_THREAD_EXIT, 90 + index);
    for (;;) Yield();
}
int Create(unsigned index, unsigned& output) {
    GtosThreadCreateRequest r = {1, 0, (unsigned)&Worker, index,
        native_thread_record.worker[index].handle, (unsigned)&output};
    return Request(GTOS_SYS_THREAD_CREATE, r);
}
void Start(unsigned index) {
    native_thread_record.worker[index].loops = 0;
    GtosThreadCreateRequest r = {1, 0, (unsigned)&Worker, index,
        native_thread_record.worker[index].handle, (unsigned)&native_thread_record.worker[index].id};
    NEED(Request(GTOS_SYS_THREAD_CREATE, r) == 0
        && native_thread_record.worker[index].id > native_thread_record.parent);
    ++native_thread_record.workerCount;
    while (native_thread_record.worker[index].stage != 1 || !native_thread_record.worker[index].loops) Yield();
    NEED(native_thread_record.worker[index].loops != 0);
}
void Pinned(unsigned index) {
    const unsigned h = native_thread_record.worker[index].handle;
    GtosVmControlRequest release = {1, h, 0}, trim = {1, h, 8192};
    NEED(Request(GTOS_SYS_VM_RELEASE, release) == GTOS_VM_ERR_CONFLICT);
    NEED(Request(GTOS_SYS_VM_TRIM, trim) == GTOS_VM_ERR_CONFLICT);
    GtosVmRangeRequest range = {1, h, 4096, 8192, 0};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, range) == GTOS_VM_ERR_CONFLICT);
    NEED(Request(GTOS_SYS_VM_DECOMMIT, range) == GTOS_VM_ERR_CONFLICT);
    NEED(Request(GTOS_SYS_VM_DISCARD, range) == GTOS_VM_ERR_CONFLICT);
}
void Completed(unsigned index) {
    unsigned output = 0xaaaaaaaa;
    int result;
    do { result = Join(native_thread_record.worker[index].id, output); if (result == GTOS_THREAD_ERR_BUSY) Yield(); }
    while (result == GTOS_THREAD_ERR_BUSY);
    NEED(result == 0 && output == 90 + index && native_thread_record.worker[index].stage == 2);
    native_thread_record.lastJoined = native_thread_record.worker[index].id;
    output = 0xaaaaaaaa;
    NEED(Join(native_thread_record.lastJoined, output) == GTOS_THREAD_ERR_NO_THREAD && output == 0xaaaaaaaa);
}
void NegativeCreate() {
    unsigned output = 0xaaaaaaaa;
    volatile unsigned* initial = (volatile unsigned*)(native_thread_record.worker[0].base + 12288 - 20);
    initial[0] = 0x12345678; initial[1] = 0x87654321;
    GtosThreadCreateRequest r = {1, 0, (unsigned)&Worker, 0,
        native_thread_record.worker[0].handle, (unsigned)&output};
    NEED(Call(GTOS_SYS_THREAD_CREATE, (unsigned)&r, sizeof(r)-1) == GTOS_THREAD_ERR_BAD_SIZE);
    NEED(Call(GTOS_SYS_THREAD_CREATE, 0x100000, sizeof(r)) == GTOS_THREAD_ERR_BAD_ADDRESS);
    r.version = 2; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_UNSUPPORTED); r.version = 1;
    r.flags = 1; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_SIZE); r.flags = 0;
    r.entry = THREAD_RECORD_VA; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_ADDRESS);
    r.entry = native_thread_record.worker[0].base + 4096;
    NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_ADDRESS); r.entry = (unsigned)&Worker;
    r.stack_handle = 0; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_STATE);
    r.stack_handle = native_thread_record.worker[0].handle;
    r.result_va = (unsigned)initial; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_ADDRESS);
    r.result_va = 0xbffffffe; NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_ADDRESS);
    r.result_va = (unsigned)&output;
    GtosVmRangeRequest protection = {1, r.stack_handle, 4096, 8192, GTOS_VM_READ};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, protection) == 0);
    NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_ADDRESS);
    protection.protection = GTOS_VM_READ_WRITE; NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, protection) == 0);
    protection.offset = 0; protection.length = 4096;
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, protection) == 0);
    NEED(Request(GTOS_SYS_THREAD_CREATE, r) == GTOS_THREAD_ERR_BAD_STATE);
    protection.protection = GTOS_VM_NONE; NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS, protection) == 0);
    NEED(output == 0xaaaaaaaa && initial[0] == 0x12345678 && initial[1] == 0x87654321);
}
void Main() {
    const GtosProcessInfoResult info = Info(); native_thread_record.parent = info.process_id;
    NEED(info.thread_id == info.process_id && info.maximum_parallel_threads == 4 && Mxcsr() == 0x1f80);
    const char path[] = "/native/thread.bin";
    GtosFileOpenRequest open = {1, (unsigned)path, sizeof(path), GTOS_FILE_READ_WRITE | GTOS_FILE_CREATE};
    const int file = Request(GTOS_SYS_FILE_OPEN, open); NEED(file > 0); native_thread_record.file = (unsigned)file;
    Reserve(0); NegativeCreate();
    unsigned output = 0xaaaaaaaa;
    NEED(Join(info.thread_id, output) == GTOS_THREAD_ERR_DEADLOCK);
    NEED(Join(0x7fffffff, output) == GTOS_THREAD_ERR_NO_THREAD && output == 0xaaaaaaaa);
    if (native_thread_record.foreignId)
        NEED(Join(native_thread_record.foreignId, output) == GTOS_THREAD_ERR_NO_THREAD && output == 0xaaaaaaaa);
    Start(0); Pinned(0);
    NEED(Join(native_thread_record.worker[0].id, output) == GTOS_THREAD_ERR_BUSY && output == 0xaaaaaaaa);
    NEED(Create(0, output) == GTOS_THREAD_ERR_BUSY && output == 0xaaaaaaaa);
    if (native_thread_record.mode == 4) {
        native_thread_record.stage = 10;
        for (;;) { ++native_thread_record.loops; NEED(Mxcsr() == 0x1f80); Yield(); }
    }
    if (native_thread_record.mode == 3) {
        native_thread_record.worker[0].go = 1; Completed(0); Release(0);
        GtosFileControlRequest close = {1, native_thread_record.file};
        NEED(Request(GTOS_SYS_FILE_CLOSE, close) == 0);
        native_thread_record.stage = 20; Call(GTOS_SYS_EXIT, 0); for (;;) Yield();
    }
    if (native_thread_record.mode) {
        Reserve(1); Start(1); Pinned(1);
        native_thread_record.stage = 10;
        if (native_thread_record.mode == 2) native_thread_record.worker[0].go = 1;
        for (;;) { ++native_thread_record.loops; NEED(Mxcsr() == 0x1f80); Yield(); }
    }
    for (unsigned index = 1; index < 4; ++index) Reserve(index);
    Start(1); Start(2);
    NEED(Create(3, output) == GTOS_THREAD_ERR_LIMIT && output == 0xaaaaaaaa);
    NEED(Mxcsr() == 0x1f80);
    for (unsigned index = 0; index < 3; ++index) native_thread_record.worker[index].go = 1;
    for (unsigned index = 0; index < 3; ++index) Completed(index);
    for (unsigned index = 1; index < 4; ++index) Release(index);
    for (unsigned loop = 0; loop < 8; ++loop) {
        native_thread_record.worker[0].stage = native_thread_record.worker[0].go = 0;
        Start(0); NEED(native_thread_record.worker[0].id > native_thread_record.lastJoined);
        native_thread_record.worker[0].go = 1;
        while (native_thread_record.worker[0].stage != 2) Yield();
        Yield(); Pinned(0); Completed(0); ++native_thread_record.loops;
    }
    Release(0);
    // Worker exit/join must leave the actual process-owned FILE usable.
    GtosFileControlRequest close = {1, native_thread_record.file};
    NEED(Request(GTOS_SYS_FILE_CLOSE, close) == 0);
    native_thread_record.finalMxcsr = Mxcsr(); NEED(native_thread_record.finalMxcsr == 0x1f80);
    native_thread_record.stage = 20; Call(GTOS_SYS_EXIT, 0);
    for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"), noreturn)) void _start() {
    Main(); for (;;) Yield();
}
