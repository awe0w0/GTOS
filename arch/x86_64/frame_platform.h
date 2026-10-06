#ifndef GTOS_X64_FRAME_PLATFORM_H
#define GTOS_X64_FRAME_PLATFORM_H
#include "frame_pool.h"

/* The single real BSP backend. It accepts only the normal boot stack, IF=0,
 * exact boot CR3, CR4=PAE, and the original supervisor hierarchy. Callers must
 * keep using this backend; none of these functions registers roots or stacks. */
extern const struct frame_platform x64_frame_platform;
volatile uint64_t *x64_frame_boot_root(void);
uint32_t x64_frame_flush_count(void);

#if VM_TEST
#include "sparse_vm.h"
/* One-shot initialization and exclusive binding to the fixed boot root. The
 * ready, free pool must use this exact backend; the VM must be unready/idle.
 * Both objects must remain resident and privately retained for kernel lifetime.
 * Failure restores the initially absent service root and publishes no binding.
 * Success returns an initialized VM: there is no externally exposed pre-ready
 * binding, caller-selected root, callback registration, reset or rebind API. */
enum vm_error x64_frame_vm_init(struct frame_pool *, struct vm_space *);
#endif
#endif
