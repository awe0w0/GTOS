#ifndef GTOS_C_FILE_PROBE_RECORD_H
#define GTOS_C_FILE_PROBE_RECORD_H
#define FILE_RECORD_VA 0x40040000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,png_bytes;
};
static_assert(sizeof(FileRecord)==72,"C FILE retained record");
#endif
