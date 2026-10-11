#ifndef GTOS_NATIVE_SEED_RECORD_H
#define GTOS_NATIVE_SEED_RECORD_H
#define FILE_RECORD_VA 0x40040000U
struct FileRecord {
    unsigned version,mode,stage,error,checks,invalid_io,mask,raw_calls;
    unsigned base,vm_handle,file,directory,progress,foreign,command,ack;
    unsigned bad_calls,result_address,feature,read_count,fault_address;
};
static_assert(sizeof(FileRecord)==84,"retained actual seed consumer record");
#endif
