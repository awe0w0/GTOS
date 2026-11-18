#ifndef GTOS_PROCESS_NATIVE_FILES_H
#define GTOS_PROCESS_NATIVE_FILES_H
#include <process/native_file_endpoint.h>
#include <process/file_abi.h>
namespace gtos { namespace storage { class FileStore; } }
namespace gtos { namespace process {
struct NativeFileStatistics { uint32_t calls,reclaims,released,reclaimFailures;int32_t lastReclaimError; };
class NativeFiles:public NativeFileEndpoint {
    storage::FileStore& store;
    uint8_t bounce[GTOS_FILE_TRANSFER_LIMIT];
    char paths[2][GTOS_FILE_PATH_LIMIT];
    NativeFileStatistics statistics;
    template<class Request>
    int32_t Snapshot(memory::ProcessAddressSpace& space,uint32_t address,uint32_t bytes,Request& request);
    int32_t ReadPathWithCheckedLayout(memory::ProcessAddressSpace& space,uint32_t address,
        uint32_t bytes,uint32_t index=0);
    NativeFiles(const NativeFiles&);
    NativeFiles& operator=(const NativeFiles&);
public:
    explicit NativeFiles(storage::FileStore& files);
    virtual int32_t Call(uint32_t owner,memory::ProcessAddressSpace& space,
        uint32_t operation,uint32_t request,uint32_t bytes);
    virtual void ReclaimOwner(uint32_t owner);
    NativeFileStatistics Statistics() const;
};
} }
#endif
