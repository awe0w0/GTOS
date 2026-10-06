#ifndef GTOS_X64_FAULT_PROBE_H
#define GTOS_X64_FAULT_PROBE_H
#include <stdint.h>
/* Narrow, explicitly armed test faults only. This is not demand paging or a
 * recovery mechanism for unexpected exceptions. count is cumulative. */
void fault_probe_scope_vm(void);
unsigned fault_probe_count(void);
void fault_probe_arm(uint64_t vector,uint64_t error,uint64_t address,void *rip,void *resume,uint64_t stack);
void fault_probe_check(unsigned expected_count);
void fault_probe_corrupt(unsigned choice);
extern void probe_read(void *),probe_write(void *),probe_execute(void *);
extern unsigned char probe_read_ip[],probe_read_resume[],probe_write_ip[],probe_write_resume[];
extern unsigned char probe_execute_resume[];
#endif
