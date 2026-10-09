#ifndef GTOS_NATIVE_FILE_PROBE_RECORD_H
#define GTOS_NATIVE_FILE_PROBE_RECORD_H
#define FILE_RECORD_VA 0x40020000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,png_bytes;
};
static_assert(sizeof(FileRecord)==72,"Actual file probe record");
#endif
