#ifndef GTOS_NATIVE_ZLIB_RECORD_H
#define GTOS_NATIVE_ZLIB_RECORD_H
#define FILE_RECORD_VA 0x40040000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,result_address,feature,read_count,fault_address,gzip_handle,foreign_handle;
    unsigned codec_result,codec_calls,codec_bytes,codec_failed,codec_frees;
    unsigned codec_before,codec_after;
};
static_assert(sizeof(FileRecord)==120,"actual codec and native handle record");
#endif
