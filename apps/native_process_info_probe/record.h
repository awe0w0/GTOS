#ifndef GTOS_NATIVE_PROCESS_INFO_RECORD_H
#define GTOS_NATIVE_PROCESS_INFO_RECORD_H
#include <process/info_abi.h>
struct ProcessInfoProbeRecord {
    unsigned version, mode, stage, checks, error, failed_queries, successful_queries, sentinel_checks;
    GtosProcessInfoResult first, last;
    unsigned keep_base, keep_handle, keep_pages, sentinel_bytes;
    unsigned last_result, raw_query_count, reserved0, reserved1;
};
static_assert(sizeof(ProcessInfoProbeRecord) == 160, "Process info probe record size");
static_assert(__builtin_offsetof(ProcessInfoProbeRecord, first) == 32, "Process info first result");
static_assert(__builtin_offsetof(ProcessInfoProbeRecord, last) == 80, "Process info last result");
#define GTOS_PROCESS_INFO_RECORD_VA 0x40020000U
#endif
