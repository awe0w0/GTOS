#ifndef GTOS_V8_PAGE_ALLOCATOR_PROBE_H
#define GTOS_V8_PAGE_ALLOCATOR_PROBE_H
#include <process/vm_abi.h>
struct VmProbeRecord { unsigned version,mode,stage,base,handle,foreign_handle; };
extern "C" volatile VmProbeRecord vm_probe_record;
extern "C" int vm_probe_call(unsigned,unsigned,unsigned);
extern "C" [[noreturn]] void vm_probe_panic(unsigned);
bool vm_probe_info(void*,GtosVmRegionInfo*);
unsigned vm_probe_handle(void*);
#endif
