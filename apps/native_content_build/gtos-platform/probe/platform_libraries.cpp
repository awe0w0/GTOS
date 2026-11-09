// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_record.h"

#include <cpuinfo.h>
#include <time.h>

#include <string>
#include <string_view>

#include "common/string_utils.h"
#include "src/dawn/utils/SystemUtils.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

extern "C" {
#include <x86/api.h>

__attribute__((section(".data.vm_record"), used))
volatile struct {
  gtos_perfetto_probe::Record result;
  uint32_t failed_checks[2];
  uint32_t detected_l1d_bytes;
  uint32_t published_l1d_count;
} native_memory_record = {{1, 0, 0, 0, 0, 0, 0}, {}, 0, 0};
}

namespace {

bool Check(bool passed) {
  auto& result = native_memory_record.result;
  const unsigned index = result.checks;
  result.checks = index + 1;
  if (!passed) {
    result.failures = result.failures + 1;
    const unsigned word = index / 32;
    const uint32_t bit = 1U << (index % 32);
    if (word < 2) {
      native_memory_record.failed_checks[word] = native_memory_record.failed_checks[word] | bit;
    }
  }
  return passed;
}

void* QueryOnWorker(void*) {
  if (!cpuinfo_initialize()) {
    return nullptr;
  }
  return const_cast<cpuinfo_processor*>(cpuinfo_get_current_processor());
}

int64_t Nanoseconds(timespec time) {
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

}  // namespace

extern "C" int NativeEntry() {
  const auto process = gtos::native::CurrentProcessInfo();
  auto& result = native_memory_record.result;
  result.base = process.process_id;
  // Compare publication against the upstream detector before platform setup.
  cpuinfo_x86_processor detected{};
  cpuinfo_x86_init_processor(&detected);
  if (!Check(cpuinfo_initialize())) {
    return 2;
  }
  const auto* cpu = cpuinfo_get_processor(0);
  if (!Check(cpu && cpu->core && cpu->cluster && cpu->package)) {
    return 3;
  }
  uint32_t eax, ebx, ecx, edx;
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
  Check(cpu->core->cpuid == eax);
  Check(cpuinfo_get_processors_count() == process.scheduler_cpu_count);
  Check(cpuinfo_get_processor(1) == nullptr && cpuinfo_get_core(1) == nullptr);
  Check(cpuinfo_get_current_processor() == cpu && cpuinfo_get_current_core() == cpu->core);
  Check(cpu->core == cpuinfo_get_core(0) && cpu->cluster == cpuinfo_get_cluster(0) &&
        cpu->package == cpuinfo_get_package(0));
  Check(cpuinfo_get_cores_count() == 1 && cpuinfo_get_clusters_count() == 1 &&
        cpuinfo_get_packages_count() == 1 && cpu->package->processor_count == 1);
  Check(cpuinfo_has_x86_sse2() == ((edx & (1U << 26)) != 0));
  Check(!cpuinfo_has_x86_avx() && !cpuinfo_has_x86_avx512f());
  const auto* cache = cpuinfo_get_l1d_cache(0);
  native_memory_record.detected_l1d_bytes = detected.cache.l1d.size;
  native_memory_record.published_l1d_count = cpuinfo_get_l1d_caches_count();
  Check(cache == cpu->cache.l1d &&
        (detected.cache.l1d.size
             ? cache && cache->size == detected.cache.l1d.size &&
                   cache->processor_start == 0 && cache->processor_count == 1
             : cache == nullptr));
  Check(cpuinfo_get_l1d_caches_count() == (detected.cache.l1d.size ? 1U : 0U) &&
        cpuinfo_get_l1d_cache(1) == nullptr &&
        cpuinfo_get_max_cache_size() >= detected.cache.l1d.size);
  uint32_t worker = 0;
  if (!Check(gtos::native::StartThread(16384, QueryOnWorker, nullptr, &worker) == 0)) {
    return 4;
  }
  result.fault_address = worker;
  void* observed = nullptr;
  Check(gtos::native::JoinThread(worker, &observed) == 0);
  Check(observed == cpu);
  Check(cpuinfo_initialize() && cpuinfo_get_processor(0) == cpu);

  using std::string_view;
  const struct {
    string_view glob;
    string_view name;
    bool matches;
  } cases[] = {
      {"", "", true},
      {"*", "", true},
      {"?", "", false},
      {"ab*", "ab", true},
      {"**?*", "", false},
      {"a*b*c", "a0b1b2c", true},
      {"a*b*c", "a0b1b2", false},
      {"*aa", "aaa", true},
      {"a?c", "abc", true},
      {"a?c", "ac", false},
      {"ab", "AB", false},
      {string_view("*\0x", 3), string_view("a\0x", 3), true},
  };
  for (const auto& test : cases) {
    Check(angle::NamesMatchWithWildcard(test.glob, test.name) == test.matches);
  }
  std::string contents = "unchanged";
  Check(!angle::ReadFileToString("/gtos-missing-file", &contents) && contents == "unchanged");
  timespec before{}, after{};
  Check(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
  dawn::utils::USleep(3000);
  Check(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
  const int64_t elapsed = Nanoseconds(after) - Nanoseconds(before);
  Check(elapsed >= 3000000 && elapsed < 1000000000);
#if defined(GTOS_PLATFORM_LIBRARIES_GUEST_WRONG_EXPECTATION)
  Check(cpuinfo_get_processors_count() == 0);
#else
  Check(cpuinfo_get_processors_count() > 0);
#endif
  result.stage = 2;
  return result.failures != 0;
}
