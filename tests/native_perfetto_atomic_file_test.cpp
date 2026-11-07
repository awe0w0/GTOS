#include "perfetto/ext/base/atomic_file.h"
#include "perfetto/ext/base/uuid.h"
#include <process/file_abi.h>
#include <errno.h>
#include <new>
#include <string.h>

namespace {
unsigned failures = 0;
unsigned stat_calls, open_calls, close_calls, duplicate_calls, sync_calls, rename_calls, remove_calls;
unsigned live_allocations = 0;
int kind, stat_error, open_error, sync_error, rename_error;
bool handle, duplicate, temporary;
const char* temp_path;
alignas(16) unsigned char heap[32][1024];
bool used[32];
void Write(const char* text) {
  unsigned bytes = 0;
  while (text[bytes]) ++bytes;
  unsigned operation = 4;
  asm volatile("int $0x80" : "+a"(operation) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Check(bool value, unsigned line = __builtin_LINE()) {
  if (value) return;
  ++failures;
  Write("CHECK_FAILED ");
  char text[10];
  for (unsigned i = 0; i < 8; ++i) text[i] = "0123456789ABCDEF"[(line >> (28 - 4 * i)) & 15];
  text[8] = '\n'; text[9] = 0; Write(text);
}
bool Path(unsigned address, unsigned bytes, const char* expected) {
  return bytes == strlen(expected) + 1 && memcmp(reinterpret_cast<const void*>(address), expected, bytes) == 0;
}
}

void* operator new(size_t bytes) {
  if (bytes > sizeof(heap[0])) { Write("ALLOCATION_UNEXPECTED\n"); __builtin_trap(); }
  for (unsigned index = 0; index < 32; ++index) if (!used[index]) {
    used[index] = true; ++live_allocations; return heap[index];
  }
  Write("ALLOCATION_EXHAUSTED\n"); __builtin_trap();
}
void operator delete(void* pointer) noexcept {
  if (!pointer) return;
  for (unsigned index = 0; index < 32; ++index) if (pointer == heap[index]) {
    Check(used[index]); used[index] = false; --live_allocations; return;
  }
  Check(false);
}
void operator delete(void* pointer, size_t) noexcept { operator delete(pointer); }
void* operator new(size_t bytes, std::align_val_t alignment) {
  Check(static_cast<unsigned>(alignment) <= 16); return operator new(bytes);
}
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { operator delete(pointer); }

extern "C" char* strerror(int) noexcept { return const_cast<char*>("test storage error"); }
extern "C" void* memmove(void* out, const void* in, size_t bytes) noexcept {
  auto* to = static_cast<unsigned char*>(out); const auto* from = static_cast<const unsigned char*>(in);
  if (to < from) { for (size_t i = 0; i < bytes; ++i) to[i] = from[i]; }
  else { while (bytes) { --bytes; to[bytes] = from[bytes]; } }
  return out;
}
extern "C" [[noreturn]] void abort() noexcept { Write("ABORT_UNEXPECTED\n"); __builtin_trap(); }
extern "C" void __assert_fail(const char*, const char*, unsigned, const char*) { abort(); }

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { abort(); }
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("ATOMIC_ASSERT\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
// Error formatting and random-number generation are boundaries here, not the
// file state machine. Real Status/string/UUID formatting remain linked.
Status ErrStatus(const char*, ...) { return Status(std::string("error")); }
Uuid GtosPerfettoTestUuid() { return Uuid(1, 2); }
}
}

extern "C" int GtosPosixFileTestCall(unsigned operation, const void* pointer, unsigned bytes) {
  if (operation == GTOS_SYS_FILE_STAT) {
    ++stat_calls;
    const auto& request = *static_cast<const GtosFileStatRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1 && request.result_bytes == sizeof(GtosFileInfo));
    Check(Path(request.path, request.path_bytes, "/target"));
    if (stat_error) return stat_error;
    if (!kind) return -2;
    auto& info = *reinterpret_cast<GtosFileInfo*>(request.result);
    info.version = 1; info.type = static_cast<unsigned>(kind); info.bytes = 17;
    return 0;
  }
  if (operation == GTOS_SYS_FILE_OPEN) {
    ++open_calls;
    const auto& request = *static_cast<const GtosFileOpenRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1 && Path(request.path, request.path_bytes, temp_path));
    Check(request.flags == (GTOS_FILE_READ_WRITE | GTOS_FILE_CREATE | GTOS_FILE_EXCLUSIVE));
    if (open_error) return open_error;
    Check(!handle && !temporary); handle = temporary = true; return 41;
  }
  if (operation == GTOS_SYS_FILE_DUP || operation == GTOS_SYS_FILE_SYNC || operation == GTOS_SYS_FILE_CLOSE) {
    const auto& request = *static_cast<const GtosFileControlRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1);
    if (operation == GTOS_SYS_FILE_DUP) {
      ++duplicate_calls; Check(request.handle == 41 && handle && !duplicate); duplicate = true; return 42;
    }
    if (operation == GTOS_SYS_FILE_SYNC) {
      ++sync_calls; Check(request.handle == 41 && handle); return sync_error;
    }
    ++close_calls;
    if (request.handle == 41) { Check(handle); handle = false; }
    else { Check(request.handle == 42 && duplicate); duplicate = false; }
    return 0;
  }
  if (operation == GTOS_SYS_FILE_RENAME) {
    ++rename_calls;
    const auto& request = *static_cast<const GtosFileRenameRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1 && Path(request.old_path, request.old_bytes, temp_path)
          && Path(request.new_path, request.new_bytes, "/target"));
    Check(temporary && !handle && !duplicate);
    if (rename_error) return rename_error;
    temporary = false; return 0;
  }
  if (operation == GTOS_SYS_FILE_REMOVE_TYPED) {
    ++remove_calls;
    const auto& request = *static_cast<const GtosFileRemoveTypedRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1 && request.type == GTOS_FILE_TYPE_REGULAR);
    Check(Path(request.path, request.path_bytes, temp_path) && temporary && !handle && !duplicate);
    temporary = false; return 0;
  }
  Check(false); return -22;
}

extern "C" int AtomicTestMain() {
  using perfetto::base::AtomicFile;
  for (unsigned repeat = 0; repeat < 2; ++repeat) {
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
      Check(!live_allocations && !handle && !duplicate && !temporary);
      stat_calls = open_calls = close_calls = duplicate_calls = sync_calls = rename_calls = remove_calls = 0;
      kind = scenario == 1 ? GTOS_FILE_TYPE_REGULAR : scenario == 2 ? GTOS_FILE_TYPE_DIRECTORY : 0;
      stat_error = scenario == 3 ? -5 : scenario == 4 ? -19 : 0;
      open_error = scenario == 5 ? -17 : 0;
      sync_error = scenario == 6 ? -5 : 0;
      rename_error = scenario == 7 ? -30 : 0;
      {
        AtomicFile file("/target");
        temp_path = file.temp_path().c_str();
        Check(file.temp_path().size() == 48 && memcmp(temp_path, "/target.tmp.", 12) == 0);
        const bool opens = scenario < 2 || scenario > 5;
        auto status = file.Open();
        Check(status.ok() == opens && stat_calls == 1 && open_calls == (scenario >= 2 && scenario <= 4 ? 0U : 1U));
        if (opens) {
          if (scenario == 0) {
            auto peer = file.DuplicateFD();
            Check(peer.get() == 42 && duplicate_calls == 1); peer.reset();
          }
          if (scenario != 8) {
            auto committed = std::move(file).Commit();
            Check(committed.ok() == (scenario < 2));
            Check(sync_calls == 1 && rename_calls == (scenario == 6 ? 0U : 1U));
          }
        }
      }
      Check(!live_allocations && !handle && !duplicate && !temporary);
      Check(close_calls == ((scenario < 2 || scenario > 5) ? (scenario == 0 ? 2U : 1U) : 0U));
      Check(remove_calls == (scenario >= 6 ? 1U : 0U));
    }
  }
#if defined(GTOS_ATOMIC_WRONG_EXPECTATION)
  Check(false);
#endif
  Write(failures ? "ATOMIC_FILE_CHECK_FAIL\n" : "ATOMIC_FILE_CHECK_PASS\n");
  return failures ? 1 : 0;
}
