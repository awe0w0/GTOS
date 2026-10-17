#ifndef GTOS_BASE_CLASS_PROBE_API_H
#define GTOS_BASE_CLASS_PROBE_API_H
struct NativeClassRecord {
  unsigned version, mode, stage, error, checks, mask, result, constructors;
  unsigned string_cases, path_cases, lifetime_waves, pma_allocations;
  unsigned mapping_handle, mapping_base, mapping_pages, mapping_released;
  unsigned reserved[48];
};
static_assert(sizeof(unsigned) == 4 && sizeof(NativeClassRecord) == 256);
extern "C" unsigned NativeClassLength(const char* text, unsigned maximum);
extern "C" unsigned NativeBaseClassChecks(volatile NativeClassRecord* record);
#endif
