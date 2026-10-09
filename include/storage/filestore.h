#ifndef GTOS_STORAGE_FILESTORE_H
#define GTOS_STORAGE_FILESTORE_H
#include <storage/blockdevice.h>
#include <storage/littlefs_port.h>
#include <storage/littlefs/lfs.h>
#undef memcpy
#undef memset
#undef memcmp
#undef strchr
#undef strcpy
#undef strspn
#undef strcspn
namespace gtos { namespace storage {
// BSP-only, serialized with IF guards. Not an SMP/threaded filesystem.
// The identified device and its exclusively owned volume must outlive Mount.
class FileStore {
public:
    enum { SectorSize=512, BlockSize=4096, SectorsPerBlock=8, MaximumHandles=64, PathLimit=512 };
    enum Error { Busy=-16, TooManyHandles=-24, ReadOnly=-30, Overflow=-75 };
    enum OpenFlag { Read=1, Write=2, ReadWrite=3, Create=0x100, Exclusive=0x200, Truncate=0x400, Append=0x800 };
    struct Info { uint32_t type,size;char name[256]; };
    struct ReclaimResult { uint32_t released;int32_t error; };
private:
    struct Slot {
        uint32_t owner,handle,mode,kind;
        lfs_file_t file;
        lfs_dir_t directory;
        lfs_file_config config;
        uint8_t cache[SectorSize];
    };
    BlockDevice* disk;
    uint32_t firstSector,volumeSectors,nextHandle;
    bool mounted,readOnly;
    lfs_t filesystem;
    lfs_config config;
    uint8_t readCache[SectorSize],progCache[SectorSize],lookahead[128],eraseCache[SectorSize];
    Slot slots[MaximumHandles];
    FileStore(const FileStore&);
    FileStore& operator=(const FileStore&);
    int32_t Configure(BlockDevice* device,uint32_t first,uint32_t sectors,bool protect);
    static int32_t ValidPath(const char* path);
    Slot* Find(uint32_t owner,uint32_t handle,uint32_t kind=0);
    Slot* FreeSlot();
    int32_t CloseSlot(Slot& slot);
    static bool ValidRegion(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,lfs_size_t size,const void* buffer);
    static int ReadBlock(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,void* buffer,lfs_size_t size);
    static int ProgBlock(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,const void* buffer,lfs_size_t size);
    static int EraseBlock(const lfs_config* cfg,lfs_block_t block);
    static int SyncBlock(const lfs_config* cfg);
    static void CopyInfo(Info& to,const lfs_info& from);
public:
    FileStore();
    // Never formats implicitly. Format is an explicit destructive operation.
    int32_t Format(BlockDevice* device,uint32_t first,uint32_t sectors);
    int32_t Mount(BlockDevice* device,uint32_t first,uint32_t sectors,bool protect=false);
    int32_t Unmount();
    bool Mounted() const { return mounted; }
    uint32_t OpenCount() const;
    int32_t Open(uint32_t owner,const char* path,uint32_t flags);
    int32_t OpenDirectory(uint32_t owner,const char* path);
    int32_t Close(uint32_t owner,uint32_t handle);
    ReclaimResult Reclaim(uint32_t owner);
    int32_t ReadFile(uint32_t owner,uint32_t handle,void* buffer,uint32_t size);
    int32_t WriteFile(uint32_t owner,uint32_t handle,const void* buffer,uint32_t size);
    int32_t Seek(uint32_t owner,uint32_t handle,int64_t offset,uint32_t whence);
    int32_t Size(uint32_t owner,uint32_t handle);
    int32_t TruncateFile(uint32_t owner,uint32_t handle,uint32_t size);
    int32_t Sync(uint32_t owner,uint32_t handle);
    int32_t ReadDirectory(uint32_t owner,uint32_t handle,Info& info);
    int32_t RewindDirectory(uint32_t owner,uint32_t handle);
    int32_t Stat(const char* path,Info& info);
    int32_t MakeDirectory(const char* path);
    int32_t Remove(const char* path);
    int32_t Rename(const char* from,const char* to);
};
} }
#endif
