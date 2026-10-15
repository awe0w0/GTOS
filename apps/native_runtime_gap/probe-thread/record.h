#ifndef GTOS_IOSTREAM_PROBE_RECORD_H
#define GTOS_IOSTREAM_PROBE_RECORD_H
#define FILE_RECORD_VA 0x40100000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,png_bytes;
    unsigned preinit,cout_sync,clog_sync,closed_failure;
    unsigned thread_ids[3],thread_stage[3];
    unsigned thread_file_done,thread_stdout_done,thread_mutex_done,thread_recursive_done;
    unsigned thread_once_enter,thread_once_done,thread_once_calls;
    unsigned thread_external_enter,thread_external_done,thread_external_calls;
    unsigned thread_notify_done,thread_joins,thread_profile,thread_blocked;
    unsigned thread_signal_wakes,thread_broadcast_wakes;
    unsigned temporary_checks,temporary_live,temporary_heap_files,helper_calls;
};
static_assert(sizeof(FileRecord)==192,"standard stream retained thread record");
#endif
