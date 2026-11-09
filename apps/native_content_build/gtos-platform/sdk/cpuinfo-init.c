// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <cpuinfo.h>
#include <cpuinfo/internal-api.h>
#include <process/info_abi.h>
#include <x86/api.h>

static struct cpuinfo_processor processor;
static struct cpuinfo_core core;
static struct cpuinfo_cluster cluster;
static struct cpuinfo_package package;
static struct cpuinfo_cache caches[cpuinfo_cache_level_max];

static const struct cpuinfo_cache* register_cache(
    enum cpuinfo_cache_level level,
    const struct cpuinfo_x86_cache* detected) {
  if (!detected->size) {
    return NULL;
  }
  struct cpuinfo_cache* cache = &caches[level];
  *cache = (struct cpuinfo_cache){
      .size = detected->size,
      .associativity = detected->associativity,
      .sets = detected->sets,
      .partitions = detected->partitions,
      .line_size = detected->line_size,
      .flags = detected->flags,
      .processor_count = 1,
  };
  cpuinfo_cache[level] = cache;
  cpuinfo_cache_count[level] = 1;
  return cache;
}

void cpuinfo_x86_gtos_init(void) {
  GtosProcessInfoResult info = {0};
  const GtosProcessInfoRequest request = {
      GTOS_PROCESS_INFO_ABI_VERSION, 0, (unsigned)(uintptr_t)&info, sizeof(info)};
  unsigned operation = GTOS_SYS_PROCESS_INFO;
  __asm__ volatile("int $0x80" : "+a"(operation)
                   : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  if (operation || info.version != GTOS_PROCESS_INFO_ABI_VERSION ||
      info.scheduler_cpu_count != 1) {
    return;
  }

  // GTOS schedules native threads on the BSP. CPUID describes that CPU's
  // implementation and caches. Its hardware topology may include other CPUs.
  struct cpuinfo_x86_processor detected = {0};
  cpuinfo_x86_init_processor(&detected);
  if (!detected.cpuid) {
    return;
  }
  cpuinfo_x86_normalize_brand_string(detected.brand_string, package.name);
  package.processor_count = package.core_count = package.cluster_count = 1;
  cluster = (struct cpuinfo_cluster){
      .processor_count = 1,
      .core_count = 1,
      .package = &package,
      .vendor = detected.vendor,
      .uarch = detected.uarch,
      .cpuid = detected.cpuid,
  };
  core = (struct cpuinfo_core){
      .processor_count = 1,
      .cluster = &cluster,
      .package = &package,
      .vendor = detected.vendor,
      .uarch = detected.uarch,
      .cpuid = detected.cpuid,
  };
  processor = (struct cpuinfo_processor){
      .core = &core,
      .cluster = &cluster,
      .package = &package,
      .apic_id = detected.topology.apic_id,
      .cache = {
          .l1i = register_cache(cpuinfo_cache_level_1i, &detected.cache.l1i),
          .l1d = register_cache(cpuinfo_cache_level_1d, &detected.cache.l1d),
          .l2 = register_cache(cpuinfo_cache_level_2, &detected.cache.l2),
          .l3 = register_cache(cpuinfo_cache_level_3, &detected.cache.l3),
          .l4 = register_cache(cpuinfo_cache_level_4, &detected.cache.l4),
      },
  };
  cpuinfo_processors = &processor;
  cpuinfo_cores = &core;
  cpuinfo_clusters = &cluster;
  cpuinfo_packages = &package;
  cpuinfo_processors_count = cpuinfo_cores_count = 1;
  cpuinfo_clusters_count = cpuinfo_packages_count = 1;
  cpuinfo_max_cache_size = cpuinfo_compute_max_cache_size(&processor);
  cpuinfo_global_uarch = (struct cpuinfo_uarch_info){
      .uarch = detected.uarch,
      .cpuid = detected.cpuid,
      .processor_count = 1,
      .core_count = 1,
  };
  cpuinfo_is_initialized = true;
}
