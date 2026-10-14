#ifndef GTOS_READONLY_PROBE_RECORD_H
#define GTOS_READONLY_PROBE_RECORD_H
#define READONLY_RECORD_VA 0x40020000U
struct ReadonlyRecord {
    unsigned version, mode, error, checks, stage;
    unsigned flags_address, flags_bytes, before, after, fault_address;
    unsigned worker, worker_stage, go;
};
#endif
