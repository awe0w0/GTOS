#include <__external_threading>
#include <chrono>
#include <condition_variable>
#include <errno.h>
#include <limits.h>
#include <mutex>
#include <time.h>
#include "absl/synchronization/internal/stdcpp_waiter.h"
#include "absl/time/time.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "record.h"

extern "C" {
volatile NativeSyncRecord native_sync_record
    __attribute__((section(".data.sync_record"), used)) = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
}
namespace {
unsigned Owner;
std::__libcpp_mutex_t RawMutex = {};
std::__libcpp_condvar_t RawCondition = {};
unsigned Call(unsigned number, const void* request, unsigned bytes) {
    asm volatile("int $0x80" : "+a"(number)
        : "b"((unsigned)request), "c"(bytes) : "memory", "cc");
    return number;
}
[[noreturn]] void Exit(unsigned result) {
    Call(GTOS_SYS_EXIT, (const void*)result, 0);
    __builtin_trap();
}
void Check(bool value, unsigned error) {
    native_sync_record.checks = native_sync_record.checks + 1;
    if (!value) { native_sync_record.error = error; Exit(99); }
}
void Held(unsigned error) {
    Check(RawMutex.owner == Owner && RawMutex.depth == 1 && RawCondition.waiters == 0, error);
}
void RawCases() {
    const unsigned before = native_sync_record.checks;
    timespec past = {-1, 999999999};
    Check(std::__libcpp_condvar_signal(nullptr) == EINVAL, 10);
    Check(std::__libcpp_condvar_broadcast(nullptr) == EINVAL, 11);
    Check(std::__libcpp_condvar_destroy(nullptr) == EINVAL, 12);
    Check(std::__libcpp_condvar_wait(nullptr, &RawMutex) == EINVAL, 13);
    Check(std::__libcpp_condvar_wait(&RawCondition, nullptr) == EINVAL, 14);
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, nullptr) == EINVAL, 15);
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &past) == EPERM, 16);
    Check(RawMutex.owner == 0 && RawMutex.depth == 0 && RawCondition.waiters == 0, 17);
    Check(std::__libcpp_mutex_lock(&RawMutex) == 0, 18);
    RawCondition.waiters = 2;
    Check(std::__libcpp_condvar_signal(&RawCondition) == EINVAL, 19);
    Check(std::__libcpp_condvar_destroy(&RawCondition) == EINVAL, 20);
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &past) == EINVAL, 21);
    RawCondition.waiters = 1;
    Check(std::__libcpp_condvar_destroy(&RawCondition) == EBUSY, 22);
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &past) == EBUSY, 23);
    RawCondition.waiters = 0;
    Held(24);
    timespec bad = {0, -1};
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &bad) == EINVAL, 25);
    bad.tv_nsec = 1000000000;
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &bad) == EINVAL, 26);
    Held(27);
    RawMutex.owner = Owner + 1;
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &past) == EPERM, 28);
    Check(RawMutex.owner == Owner + 1 && RawMutex.depth == 1 && !RawCondition.waiters, 29);
    RawMutex.owner = Owner;
    RawCondition.sequence = UINT_MAX;
    Check(std::__libcpp_condvar_signal(&RawCondition) == 0 && RawCondition.sequence == 0, 30);
    Check(std::__libcpp_condvar_broadcast(&RawCondition) == 0 && RawCondition.sequence == 1, 31);
    errno = EDOM;
    Check(std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &past) == ETIMEDOUT, 32);
    Check(errno == EDOM && RawCondition.sequence == 1, 33);
    Held(34);
    Check(std::__libcpp_mutex_unlock(&RawMutex) == 0, 35);
    Check(std::__libcpp_condvar_destroy(&RawCondition) == 0, 36);
    Check(std::__libcpp_mutex_destroy(&RawMutex) == 0, 37);
    native_sync_record.rawChecks = native_sync_record.checks - before;
}
void StandardCases() {
    const unsigned before = native_sync_record.checks;
    std::mutex mutex;
    Check(mutex.try_lock(), 40);
    Check(!mutex.try_lock(), 41);
    Check(mutex.native_handle()->owner == Owner && mutex.native_handle()->depth == 1, 42);
    mutex.unlock();
    std::condition_variable condition;
    std::unique_lock<std::mutex> lock(mutex);
    condition.notify_one();
    condition.notify_all();
    Check(condition.wait_until(lock, std::chrono::system_clock::time_point{}) == std::cv_status::timeout, 43);
    Check(lock.owns_lock() && mutex.native_handle()->owner == Owner, 44);
    const auto beforeWait = std::chrono::steady_clock::now();
    Check(condition.wait_for(lock, std::chrono::milliseconds(30)) == std::cv_status::timeout, 45);
    Check(std::chrono::steady_clock::now() - beforeWait >= std::chrono::milliseconds(30), 46);
    Check(lock.owns_lock() && mutex.native_handle()->owner == Owner, 47);
    Check(condition.wait_for(lock, std::chrono::nanoseconds(-1)) == std::cv_status::timeout, 48);
    Check(condition.wait_for(lock, std::chrono::milliseconds(30), [] { return true; }), 49);
    Check(!condition.wait_for(lock, std::chrono::nanoseconds(0), [] { return false; }), 50);
    lock.unlock();
    std::recursive_mutex recursive;
    recursive.lock(); recursive.lock();
    Check(recursive.try_lock(), 51);
    recursive.unlock(); recursive.unlock(); recursive.unlock();
    std::timed_mutex timed;
    Check(timed.try_lock(), 52);
    Check(!timed.try_lock(), 53);
    timed.unlock();
    std::recursive_timed_mutex recursiveTimed;
    recursiveTimed.lock();
    Check(recursiveTimed.try_lock(), 54);
    recursiveTimed.unlock(); recursiveTimed.unlock();
    native_sync_record.stdChecks = native_sync_record.checks - before;
}
void WaiterCases() {
    using absl::synchronization_internal::KernelTimeout;
    using absl::synchronization_internal::StdcppWaiter;
    const unsigned before = native_sync_record.checks;
    StdcppWaiter waiter;
    waiter.Post(); waiter.Post();
    Check(waiter.Wait(KernelTimeout::Never()), 60);
    Check(waiter.Wait(KernelTimeout::Never()), 61);
    waiter.Poke();
    Check(!waiter.Wait(KernelTimeout(absl::ZeroDuration())), 62);
    const auto began = std::chrono::steady_clock::now();
    Check(!waiter.Wait(KernelTimeout(absl::Milliseconds(30))), 63);
    Check(std::chrono::steady_clock::now() - began >= std::chrono::milliseconds(30), 64);
    waiter.Post();
    Check(waiter.Wait(KernelTimeout(absl::ZeroDuration())), 65);
    native_sync_record.waiterChecks = native_sync_record.checks - before;
}
}
extern "C" int NativeV8IsolateMain() {
    Owner = v8::base::gtos::CurrentProcessInfo().thread_id;
    Check(Owner != 0, 1);
    GtosVmReserveResult result = {};
    GtosVmReserveRequest reserve = {1, 4096, 4096, 0x80000000U, (unsigned)&result};
    Check(Call(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve)) == 0, 2);
    GtosVmRangeRequest commit = {1, result.handle, 0, 4096, GTOS_VM_READ_WRITE};
    Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit)) == 0, 3);
    Check(result.base == 0x80000000U && result.length == 4096 && *(volatile unsigned*)result.base == 0, 4);
    *(volatile unsigned*)result.base = 0xC011CA11U;
    native_sync_record.region = result.handle;
    native_sync_record.base = result.base;
    native_sync_record.length = result.length;
    native_sync_record.mutexAddress = (unsigned)&RawMutex;
    native_sync_record.conditionAddress = (unsigned)&RawCondition;
    native_sync_record.stage = 1;
    RawCases(); StandardCases(); WaiterCases();
    native_sync_record.stage = 2;
    const unsigned mode = native_sync_record.mode;
    if (mode == 1) {
        std::mutex mutex;
        std::condition_variable condition;
        std::unique_lock<std::mutex> unowned(mutex, std::defer_lock);
        condition.wait(unowned);
        Exit(98);
    }
    if (mode == 2 || mode == 3 || mode == 4) {
        Check(std::__libcpp_mutex_lock(&RawMutex) == 0, 70);
        native_sync_record.pending = mode;
        errno = EDOM;
        timespec future = {0x7FFFFFFFFFFFFFFFLL, 999999999};
        const int status = mode == 2 ? std::__libcpp_condvar_wait(&RawCondition, &RawMutex)
                                   : std::__libcpp_condvar_timedwait(&RawCondition, &RawMutex, &future);
        if (mode != 4) Exit(98);
        // The kernel fixture injects one real clock exhaustion in this mode.
        Check(status == EOVERFLOW && errno == EDOM, 71);
        Held(72);
        Check(std::__libcpp_mutex_unlock(&RawMutex) == 0, 73);
        native_sync_record.clockErrors = 1;
        native_sync_record.pending = 0;
    }
    native_sync_record.stage = 3;
    return 0;
}
