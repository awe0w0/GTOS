#ifndef GTOS_IOSTREAM_PROBE_RECORD_H
#define GTOS_IOSTREAM_PROBE_RECORD_H
#define FILE_RECORD_VA 0x40080000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,png_bytes;
    unsigned preinit,cout_sync,clog_sync,closed_failure;
};
static_assert(sizeof(FileRecord)==88,"standard stream retained record");
#endif
