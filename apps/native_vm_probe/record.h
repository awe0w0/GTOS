#ifndef GTOS_NATIVE_VM_PROBE_RECORD_H
#define GTOS_NATIVE_VM_PROBE_RECORD_H
#include <common/types.h>
#define GTOS_VM_PROBE_RECORD_ADDRESS 0x40020000U
struct VmProbeRecord {
    uint32_t version, mode, stage, base, handle, foreign_handle;
};
static_assert(sizeof(VmProbeRecord) == 24, "VM probe record wire size");
static_assert(__builtin_offsetof(VmProbeRecord, foreign_handle) == 20, "actual peer handle field");
extern "C" volatile VmProbeRecord native_vm_record;
#endif
