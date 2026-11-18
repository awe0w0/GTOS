#include <process/native_files.h>
#include <process/abi.h>
#include <storage/filestore.h>
#include <memory/criticalsection.h>
using namespace gtos::process;
using gtos::memory::ProcessAddressSpace;
using gtos::memory::InterruptGuard;
using gtos::storage::FileStore;
static_assert(FileStore::Read==GTOS_FILE_READ&&FileStore::Write==GTOS_FILE_WRITE
    &&FileStore::ReadWrite==GTOS_FILE_READ_WRITE&&FileStore::Create==GTOS_FILE_CREATE
    &&FileStore::Exclusive==GTOS_FILE_EXCLUSIVE&&FileStore::Truncate==GTOS_FILE_TRUNCATE
    &&FileStore::Append==GTOS_FILE_APPEND&&FileStore::Temporary==GTOS_FILE_TEMPORARY,"Real store open flags");
static_assert(LFS_TYPE_REG==GTOS_FILE_TYPE_REGULAR&&LFS_TYPE_DIR==GTOS_FILE_TYPE_DIRECTORY,"Real file types");
namespace {
    int64_t Offset(uint32_t low,uint32_t high) {
        return (int64_t)(int32_t)high*4294967296LL+low;
    }
    GtosFileInfo Info(const FileStore::Info& value) {
        GtosFileInfo result={};result.version=GTOS_FILE_ABI_VERSION;result.type=value.type;result.bytes=value.size;
        for (uint32_t i=0;i<sizeof(result.name);i++) result.name[i]=value.name[i];
        return result;
    }
}
NativeFiles::NativeFiles(FileStore& files):store(files),statistics() {}
template<class Request>
int32_t NativeFiles::Snapshot(ProcessAddressSpace& space,uint32_t address,uint32_t bytes,Request& request) {
    if (bytes!=sizeof(request)) return GTOS_FILE_ERR_INVALID;
    if (!space.CheckCopyLayout()||!space.CopyFromUserWithCheckedLayout(&request,address,sizeof(request)))
        return GTOS_FILE_ERR_BAD_ADDRESS;
    return request.version==GTOS_FILE_ABI_VERSION?0:GTOS_FILE_ERR_VERSION;
}
int32_t NativeFiles::ReadPathWithCheckedLayout(ProcessAddressSpace& space,uint32_t address,
        uint32_t bytes,uint32_t index) {
    if (bytes<2) return GTOS_FILE_ERR_INVALID;
    if (bytes>GTOS_FILE_PATH_LIMIT) return GTOS_FILE_ERR_TOO_LARGE;
    if (!space.CopyFromUserWithCheckedLayout(paths[index],address,bytes)) return GTOS_FILE_ERR_BAD_ADDRESS;
    if (paths[index][bytes-1]) return GTOS_FILE_ERR_INVALID;
    for (uint32_t i=0;i<bytes-1;i++) if (!paths[index][i]) return GTOS_FILE_ERR_INVALID;
    return 0;
}
int32_t NativeFiles::Call(uint32_t owner,ProcessAddressSpace& space,uint32_t operation,uint32_t address,uint32_t bytes) {
    InterruptGuard guard;statistics.calls++;
    if (!owner||!store.Mounted()) return GTOS_FILE_ERR_UNAVAILABLE;
    // After Snapshot audits the layout, reuse it under this interrupt guard.
    // File operations preserve the process mappings.
    switch (operation) {
    case GTOS_SYS_FILE_OPEN: {
        GtosFileOpenRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        error=ReadPathWithCheckedLayout(space,request.path,request.path_bytes);if (error) return error;
        return store.Open(owner,paths[0],request.flags);
    }
    case GTOS_SYS_FILE_READ:
    case GTOS_SYS_FILE_READ_AT:
    case GTOS_SYS_FILE_WRITE_AT:
    case GTOS_SYS_FILE_WRITE: {
        const bool positioned=operation==GTOS_SYS_FILE_READ_AT||operation==GTOS_SYS_FILE_WRITE_AT;
        const bool reading=operation==GTOS_SYS_FILE_READ||operation==GTOS_SYS_FILE_READ_AT;
        GtosFileTransferRequest request;
        int64_t offset=0;
        if (positioned) {
            GtosFilePositionedTransferRequest position;
            int32_t error=Snapshot(space,address,bytes,position);if (error) return error;
            request={position.version,position.handle,position.buffer,position.bytes};
            offset=Offset(position.offset_low,position.offset_high);
        } else {
            int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        }
        if (request.bytes>GTOS_FILE_TRANSFER_LIMIT) return GTOS_FILE_ERR_TOO_LARGE;
        auto read=[&](void* buffer,uint32_t length) {
            return positioned?store.ReadFileAt(owner,request.handle,offset,buffer,length)
                :store.ReadFile(owner,request.handle,buffer,length);
        };
        auto write=[&](const void* buffer,uint32_t length) {
            return positioned?store.WriteFileAt(owner,request.handle,offset,buffer,length)
                :store.WriteFile(owner,request.handle,buffer,length);
        };
        int32_t error=reading?read(0,0):write(0,0);
        if (error) return error;
        if (!space.CheckCopyRange(request.buffer,request.bytes,reading)) return GTOS_FILE_ERR_BAD_ADDRESS;
        if (!request.bytes) return 0;
        if (!reading) {
            if (!space.CopyFromUserWithCheckedLayout(bounce,request.buffer,request.bytes)) return GTOS_FILE_ERR_BAD_ADDRESS;
            return write(bounce,request.bytes);
        }
        int32_t count=read(bounce,request.bytes);
        if (count<=0) return count;
        return space.CopyToUserWithCheckedLayout(request.buffer,bounce,count)?count:GTOS_FILE_ERR_BAD_ADDRESS;
    }
    case GTOS_SYS_FILE_SEEK: {
        GtosFileSeekRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        const int64_t offset=Offset(request.offset_low,request.offset_high);
        return store.Seek(owner,request.handle,offset,request.whence);
    }
    case GTOS_SYS_FILE_STREAM_MODE: {
        GtosFileStreamModeRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        return store.ConfigureStream(owner,request.handle,request.flags);
    }
    case GTOS_SYS_FILE_CLOSE:
    case GTOS_SYS_FILE_SYNC:
    case GTOS_SYS_FILE_SIZE:
    case GTOS_SYS_FILE_DUP:
    case GTOS_SYS_FILE_DIR_REWIND: {
        GtosFileControlRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        if (operation==GTOS_SYS_FILE_CLOSE) return store.Close(owner,request.handle);
        if (operation==GTOS_SYS_FILE_SYNC) return store.Sync(owner,request.handle);
        if (operation==GTOS_SYS_FILE_SIZE) return store.Size(owner,request.handle);
        if (operation==GTOS_SYS_FILE_DUP) return store.Duplicate(owner,request.handle);
        return store.RewindDirectory(owner,request.handle);
    }
    case GTOS_SYS_FILE_TRUNCATE: {
        GtosFileTruncateRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        return store.TruncateFile(owner,request.handle,request.bytes);
    }
    case GTOS_SYS_FILE_MKDIR:
    case GTOS_SYS_FILE_REMOVE:
    case GTOS_SYS_FILE_DIR_OPEN: {
        GtosFilePathRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        error=ReadPathWithCheckedLayout(space,request.path,request.path_bytes);if (error) return error;
        if (operation==GTOS_SYS_FILE_MKDIR) return store.MakeDirectory(paths[0]);
        if (operation==GTOS_SYS_FILE_REMOVE) return store.Remove(paths[0]);
        return store.OpenDirectory(owner,paths[0]);
    }
    case GTOS_SYS_FILE_RENAME: {
        GtosFileRenameRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        error=ReadPathWithCheckedLayout(space,request.old_path,request.old_bytes);if (error) return error;
        error=ReadPathWithCheckedLayout(space,request.new_path,request.new_bytes,1);if (error) return error;
        return store.Rename(paths[0],paths[1]);
    }
    case GTOS_SYS_FILE_REMOVE_TYPED: {
        GtosFileRemoveTypedRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        if (request.type!=GTOS_FILE_TYPE_REGULAR&&request.type!=GTOS_FILE_TYPE_DIRECTORY) return GTOS_FILE_ERR_INVALID;
        error=ReadPathWithCheckedLayout(space,request.path,request.path_bytes);if (error) return error;
        FileStore::Info value={};error=store.Stat(paths[0],value);if (error) return error;
        if (value.type!=request.type) return request.type==GTOS_FILE_TYPE_REGULAR?LFS_ERR_ISDIR:LFS_ERR_NOTDIR;
        return store.Remove(paths[0]);
    }
    case GTOS_SYS_FILE_STAT: {
        GtosFileStatRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        error=ReadPathWithCheckedLayout(space,request.path,request.path_bytes);if (error) return error;
        if (request.result_bytes!=GTOS_FILE_INFO_BYTES) return GTOS_FILE_ERR_INVALID;
        if (!space.CheckCopyRange(request.result,sizeof(GtosFileInfo),true)) return GTOS_FILE_ERR_BAD_ADDRESS;
        FileStore::Info value={};error=store.Stat(paths[0],value);if (error) return error;
        const GtosFileInfo result=Info(value);
        return space.CopyToUserWithCheckedLayout(request.result,&result,sizeof(result))?0:GTOS_FILE_ERR_BAD_ADDRESS;
    }
    case GTOS_SYS_FILE_DIR_READ:
    case GTOS_SYS_FILE_HANDLE_INFO: {
        GtosFileInfoRequest request;int32_t error=Snapshot(space,address,bytes,request);if (error) return error;
        if (request.result_bytes!=GTOS_FILE_INFO_BYTES) return GTOS_FILE_ERR_INVALID;
        if (!space.CheckCopyRange(request.result,sizeof(GtosFileInfo),true)) return GTOS_FILE_ERR_BAD_ADDRESS;
        FileStore::Info value={};
        const bool directory=operation==GTOS_SYS_FILE_DIR_READ;
        int32_t count=directory?store.ReadDirectory(owner,request.handle,value):store.HandleInfo(owner,request.handle,value);
        if (count<0||(directory&&!count)) return count;
        const GtosFileInfo result=Info(value);
        return space.CopyToUserWithCheckedLayout(request.result,&result,sizeof(result))?count:GTOS_FILE_ERR_BAD_ADDRESS;
    }
    default:return GTOS_ERR_UNSUPPORTED;
    }
}
void NativeFiles::ReclaimOwner(uint32_t owner) {
    InterruptGuard guard;statistics.reclaims++;
    statistics.lastReclaimError=0;
    // An unmounted store cannot retain handles: Unmount refuses open slots.
    // No filesystem resource exists to close, and no disk access is needed.
    if (!store.Mounted()) return;
    const FileStore::ReclaimResult result=store.Reclaim(owner);statistics.released+=result.released;
    statistics.lastReclaimError=result.error;if (result.error) statistics.reclaimFailures++;
}
NativeFileStatistics NativeFiles::Statistics() const { InterruptGuard guard;return statistics; }
