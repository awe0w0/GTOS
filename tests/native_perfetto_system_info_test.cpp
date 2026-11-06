#include "perfetto/ext/base/android_utils.h"
#include "perfetto/base/logging.h"
#include <new>
#include <process/info_abi.h>

namespace {
unsigned failed = 0;
unsigned calls = 0;
unsigned invalid = 0;
unsigned logs = 0;
const char* machine_name = nullptr;
alignas(16) unsigned char storage[4096];
bool allocated = false;
unsigned allocations = 0;
unsigned releases = 0;
void Write(const char* text) {
  unsigned size = 0;
  while (text[size]) ++size;
  unsigned operation = 4;
  asm volatile("int $0x80" : "+a"(operation) : "b"(1), "c"(text), "d"(size) : "memory", "cc");
}
void Check(bool value, unsigned line = __builtin_LINE()) {
  if (!value) {
    ++failed;
    Write("CHECK_FAILED ");
    char text[10];
    for (unsigned index = 0; index < 8; ++index) text[index] = "0123456789ABCDEF"[(line >> (28 - 4 * index)) & 15];
    text[8] = '\n'; text[9] = 0;
    Write(text);
  }
}
}

// Only the allocator/environment/syscall dependencies are replaced. The actual
// libc++ string/optional/vector code and native SystemInfo body are compiled.
void* operator new(size_t bytes) {
  if (allocated || bytes > sizeof(storage)) { Write("ALLOCATION_UNEXPECTED\n"); __builtin_trap(); }
  allocated = true; ++allocations;
  return storage;
}
void operator delete(void* pointer) noexcept {
  if (!pointer) return;
  Check(pointer == storage && allocated);
  allocated = false; ++releases;
}
void operator delete(void* pointer, size_t) noexcept { operator delete(pointer); }
void* operator new(size_t bytes, std::align_val_t alignment) {
  Check(static_cast<unsigned>(alignment) <= 16);
  return operator new(bytes);
}
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { operator delete(pointer); }

extern "C" {
int* __llvm_libc_errno() noexcept { static int value; return &value; }
char* strerror(int) noexcept { return const_cast<char*>("test info error"); }
char* getenv(const char* name) noexcept {
  const char* expected = "PERFETTO_MACHINE_NAME";
  unsigned index = 0;
  while (name[index] && expected[index] && name[index] == expected[index]) ++index;
  Check(!name[index] && !expected[index]);
  return const_cast<char*>(machine_name);
}
unsigned strlen(const char* value) { unsigned count = 0; while (value[count]) ++count; return count; }
void* memcpy(void* to, const void* from, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) static_cast<char*>(to)[i] = static_cast<const char*>(from)[i];
  return to;
}
void* memmove(void* to, const void* from, unsigned bytes) {
  auto* output = static_cast<unsigned char*>(to);
  const auto* input = static_cast<const unsigned char*>(from);
  if (output < input) { for (unsigned i = 0; i < bytes; ++i) output[i] = input[i]; }
  else { while (bytes) { --bytes; output[bytes] = input[bytes]; } }
  return to;
}
void* memset(void* to, int byte, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) static_cast<char*>(to)[i] = static_cast<char>(byte);
  return to;
}
int memcmp(const void* first, const void* second, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) {
    auto a = static_cast<const unsigned char*>(first)[i], b = static_cast<const unsigned char*>(second)[i];
    if (a != b) return a < b ? -1 : 1;
  }
  return 0;
}
__attribute__((noreturn)) void abort() noexcept { Write("ABORT_UNEXPECTED\n"); __builtin_trap(); }
void __assert_fail(const char*, const char*, unsigned, const char*) { abort(); }
int GtosPerfettoInfoTestCall(unsigned operation, const void* pointer, unsigned bytes) {
  ++calls;
  const auto& request = *static_cast<const GtosProcessInfoRequest*>(pointer);
  Check(operation == GTOS_SYS_PROCESS_INFO && bytes == sizeof(request));
  Check(request.version == GTOS_PROCESS_INFO_ABI_VERSION && request.flags == 0 &&
        request.result_va && request.result_bytes == sizeof(GtosProcessInfoResult));
  GtosProcessInfoResult value{1, 19, 23, 0xBFFFA000, 0xBFFFC000, 0x40000000,
                              0xC0000000, 4096, 65536, 16, 4, 1};
  switch (invalid) {
  case 1: return GTOS_PROCESS_INFO_ERR_UNSUPPORTED;
  case 2: ++value.version; break;
  case 3: value.process_id = 0; break;
  case 4: value.thread_id = 0; break;
  case 5: value.page_bytes = 0; break;
  case 6: value.page_bytes = 8192; break;
  case 7: value.scheduler_cpu_count = 0; break;
  case 8: value.scheduler_cpu_count = 2; break;
  }
  *reinterpret_cast<GtosProcessInfoResult*>(request.result_va) = value;
  return 0;
}
}

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { abort(); }
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { ++logs; }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int InfoTestMain() {
  using namespace perfetto::base;
  {
    auto value = GetUtsname();
    Check(value.sysname == "GTOS" && value.machine == "i686");
    Check(value.version.empty() && value.release.empty() && calls == 0);
  }
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    for (invalid = 0; invalid <= 8; ++invalid) {
      const auto before = calls;
      auto value = GetSystemInfo();
      Check(calls == before + 1);
      Check(value.utsname_info.has_value() && value.utsname_info->sysname == "GTOS" &&
            value.utsname_info->machine == "i686");
      Check(!value.timezone_off_mins.has_value() && !value.system_ram_bytes.has_value());
      Check(value.android_build_fingerprint.empty() && value.android_device_manufacturer.empty() &&
            !value.android_sdk_version.has_value() && value.android_soc_model.empty() &&
            value.android_guest_soc_model.empty() && value.android_hardware_revision.empty() &&
            value.android_storage_model.empty() && value.android_ram_model.empty() &&
            value.android_serial_console.empty() && value.device_tree_compatibles.empty());
      if (invalid) {
        Check(!value.page_size.has_value() && !value.num_cpus.has_value());
      } else {
        Check(value.page_size.has_value() && *value.page_size == 4096 &&
              value.num_cpus.has_value() && *value.num_cpus == 1);
      }
    }
  }
  Check(logs == 16 && calls == 18);
  for (unsigned iteration = 0; iteration < 4; ++iteration) {
    machine_name = nullptr;
    Check(GetPerfettoMachineName() == "GTOS");
    machine_name = "";
    Check(GetPerfettoMachineName().empty());
    machine_name = "native-test";
    Check(GetPerfettoMachineName() == "native-test");
    machine_name = "a-name-that-exceeds-libcxx-short-string-storage-for-the-native-test";
    Check(GetPerfettoMachineName() == machine_name);
    Check(!allocated && allocations == releases);
  }
  // IA32 libc++ stores only ten characters inline; both the eleven-character
  // name and the long name allocate once per iteration.
  Check(allocations == 8 && releases == 8);
#if defined(GTOS_INFO_WRONG_EXPECTATION)
  Check(GetPerfettoMachineName() == "wrong");
#endif
  Write(failed ? "SYSTEM_INFO_CHECK_FAIL\n" : "SYSTEM_INFO_CHECK_PASS\n");
  return failed ? 1 : 0;
}
