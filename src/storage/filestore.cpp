#include <storage/filestore.h>
#include <memory/criticalsection.h>
using namespace gtos::storage;
using gtos::memory::InterruptGuard;
FileStore::FileStore():disk(0),firstSector(0),volumeSectors(0),nextHandle(1),mounted(false),readOnly(false) {
    gtos_lfs_memset(&filesystem,0,sizeof(filesystem));gtos_lfs_memset(&config,0,sizeof(config));
    gtos_lfs_memset(slots,0,sizeof(slots));
    gtos_lfs_memset(eraseCache,255,sizeof(eraseCache));
}
int32_t FileStore::Configure(BlockDevice* device,uint32_t first,uint32_t sectors,bool protect) {
    if (!device||first%SectorsPerBlock||sectors%SectorsPerBlock||sectors<2*SectorsPerBlock
        ||first>device->SectorCount()||sectors>device->SectorCount()-first) return LFS_ERR_INVAL;
    disk=device;firstSector=first;volumeSectors=sectors;readOnly=protect;
    gtos_lfs_memset(&filesystem,0,sizeof(filesystem));gtos_lfs_memset(&config,0,sizeof(config));
    config.context=this;config.read=ReadBlock;config.prog=ProgBlock;config.erase=EraseBlock;config.sync=SyncBlock;
    config.read_size=SectorSize;config.prog_size=SectorSize;config.block_size=BlockSize;
    config.block_count=sectors/SectorsPerBlock;config.block_cycles=500;config.cache_size=SectorSize;
    config.lookahead_size=sizeof(lookahead);config.read_buffer=readCache;config.prog_buffer=progCache;
    config.lookahead_buffer=lookahead;
    return 0;
}
bool FileStore::ValidRegion(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,lfs_size_t size,const void* buffer) {
    return cfg&&cfg->context&&block<cfg->block_count&&off<=BlockSize&&size<=BlockSize-off
        &&off%SectorSize==0&&size%SectorSize==0&&(!size||buffer);
}
int FileStore::ReadBlock(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,void* buffer,lfs_size_t size) {
    if (!ValidRegion(cfg,block,off,size,buffer)) return LFS_ERR_INVAL;
    FileStore* self=(FileStore*)cfg->context;
    uint32_t start=self->firstSector+block*SectorsPerBlock+off/SectorSize;
    for (uint32_t i=0;i<size/SectorSize;i++)
        if (!self->disk->ReadSector(start+i,(uint8_t*)buffer+i*SectorSize)) return LFS_ERR_IO;
    return 0;
}
int FileStore::ProgBlock(const lfs_config* cfg,lfs_block_t block,lfs_off_t off,const void* buffer,lfs_size_t size) {
    if (!ValidRegion(cfg,block,off,size,buffer)) return LFS_ERR_INVAL;
    FileStore* self=(FileStore*)cfg->context;if (self->readOnly) return ReadOnly;
    uint32_t start=self->firstSector+block*SectorsPerBlock+off/SectorSize;
    for (uint32_t i=0;i<size/SectorSize;i++)
        if (!self->disk->WriteSector(start+i,(const uint8_t*)buffer+i*SectorSize)) return LFS_ERR_IO;
    return 0;
}
int FileStore::EraseBlock(const lfs_config* cfg,lfs_block_t block) {
    if (!ValidRegion(cfg,block,0,BlockSize,cfg)) return LFS_ERR_INVAL;
    FileStore* self=(FileStore*)cfg->context;if (self->readOnly) return ReadOnly;
    for (uint32_t i=0;i<SectorsPerBlock;i++)
        if (!self->disk->WriteSector(self->firstSector+block*SectorsPerBlock+i,self->eraseCache)) return LFS_ERR_IO;
    return 0;
}
int FileStore::SyncBlock(const lfs_config* cfg) {
    FileStore* self=(FileStore*)cfg->context;
    if (self->readOnly) return 0;
    return self->disk->Flush()?0:LFS_ERR_IO;
}
int32_t FileStore::Format(BlockDevice* device,uint32_t first,uint32_t sectors) {
    InterruptGuard guard;if (mounted) return Busy;
    int32_t error=Configure(device,first,sectors,false);if (error) return error;
    return lfs_format(&filesystem,&config);
}
int32_t FileStore::Mount(BlockDevice* device,uint32_t first,uint32_t sectors,bool protect) {
    InterruptGuard guard;if (mounted) return Busy;
    int32_t error=Configure(device,first,sectors,protect);if (error) return error;
    error=lfs_mount(&filesystem,&config);mounted=error==0;return error;
}
uint32_t FileStore::OpenCount() const {
    InterruptGuard guard;uint32_t count=0;
    for (uint32_t i=0;i<MaximumHandles;i++) if (slots[i].handle) count++;
    return count;
}
int32_t FileStore::Unmount() {
    InterruptGuard guard;if (!mounted) return LFS_ERR_BADF;
    if (OpenCount()) return Busy;
    int32_t error=lfs_unmount(&filesystem);if (!error) mounted=false;return error;
}
int32_t FileStore::ValidPath(const char* path) {
    if (!path||!path[0]) return LFS_ERR_INVAL;
    for (uint32_t i=0;i<PathLimit;i++) if (!path[i]) return 0;
    return LFS_ERR_NAMETOOLONG;
}
FileStore::Slot* FileStore::Find(uint32_t owner,uint32_t handle,uint32_t kind) {
    if (!mounted||!owner||!handle) return 0;
    for (uint32_t i=0;i<MaximumHandles;i++)
        if (slots[i].handle==handle&&slots[i].owner==owner&&(!kind||slots[i].kind==kind)) return slots+i;
    return 0;
}
FileStore::Slot* FileStore::FreeSlot() {
    for (uint32_t i=0;i<MaximumHandles;i++) if (!slots[i].handle) return slots+i;
    return 0;
}
int32_t FileStore::Open(uint32_t owner,const char* path,uint32_t flags) {
    InterruptGuard guard;if (!mounted||!owner) return LFS_ERR_BADF;
    int32_t error=ValidPath(path);if (error) return error;
    uint32_t access=flags&3;
    if (!access||(flags&~(3U|Create|Exclusive|Truncate|Append))
        ||((flags&Exclusive)&&!(flags&Create))||((flags&(Truncate|Append))&&access==Read)) return LFS_ERR_INVAL;
    if (readOnly&&(access!=Read||(flags&Create))) return ReadOnly;
    if (nextHandle>0x7FFFFFFFU) return Overflow;
    Slot* slot=FreeSlot();if (!slot) return TooManyHandles;
    gtos_lfs_memset(slot,0,sizeof(*slot));slot->config.buffer=slot->cache;
    error=lfs_file_opencfg(&filesystem,&slot->file,path,flags,&slot->config);if (error) return error;
    slot->owner=owner;slot->handle=nextHandle++;slot->mode=access;slot->kind=LFS_TYPE_REG;
    return slot->handle;
}
int32_t FileStore::OpenDirectory(uint32_t owner,const char* path) {
    InterruptGuard guard;if (!mounted||!owner) return LFS_ERR_BADF;
    int32_t error=ValidPath(path);if (error) return error;
    if (nextHandle>0x7FFFFFFFU) return Overflow;
    Slot* slot=FreeSlot();if (!slot) return TooManyHandles;
    gtos_lfs_memset(slot,0,sizeof(*slot));
    error=lfs_dir_open(&filesystem,&slot->directory,path);if (error) return error;
    slot->owner=owner;slot->handle=nextHandle++;slot->kind=LFS_TYPE_DIR;return slot->handle;
}
int32_t FileStore::CloseSlot(Slot& slot) {
    int32_t error=slot.kind==LFS_TYPE_REG?lfs_file_close(&filesystem,&slot.file):lfs_dir_close(&filesystem,&slot.directory);
    // Upstream close unlinks even after sync failure; always invalidate the handle.
    slot.handle=0;slot.owner=0;slot.kind=0;return error;
}
int32_t FileStore::Close(uint32_t owner,uint32_t handle) {
    InterruptGuard guard;Slot* slot=Find(owner,handle);return slot?CloseSlot(*slot):LFS_ERR_BADF;
}
FileStore::ReclaimResult FileStore::Reclaim(uint32_t owner) {
    InterruptGuard guard;ReclaimResult result={0,0};
    if (!mounted||!owner) { result.error=LFS_ERR_BADF;return result; }
    for (uint32_t i=0;i<MaximumHandles;i++) if (slots[i].handle&&slots[i].owner==owner) {
        int32_t error=CloseSlot(slots[i]);result.released++;
        if (error&&!result.error) result.error=error;
    }
    return result;
}
int32_t FileStore::ReadFile(uint32_t owner,uint32_t handle,void* buffer,uint32_t size) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);
    if (!slot||slot->mode==Write) return LFS_ERR_BADF;
    if ((!buffer&&size)||size>0x7FFFFFFFU) return LFS_ERR_INVAL;
    if (!size) return 0;
    return lfs_file_read(&filesystem,&slot->file,buffer,size);
}
int32_t FileStore::WriteFile(uint32_t owner,uint32_t handle,const void* buffer,uint32_t size) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);
    if (!slot||slot->mode==Read) return LFS_ERR_BADF;
    if (readOnly) return ReadOnly;
    if ((!buffer&&size)||size>0x7FFFFFFFU) return LFS_ERR_INVAL;
    if (!size) return 0;
    return lfs_file_write(&filesystem,&slot->file,buffer,size);
}
int32_t FileStore::Seek(uint32_t owner,uint32_t handle,int64_t offset,uint32_t whence) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);if (!slot) return LFS_ERR_BADF;
    if (whence>2) return LFS_ERR_INVAL;
    int32_t origin=whence==1?lfs_file_tell(&filesystem,&slot->file):whence==2?lfs_file_size(&filesystem,&slot->file):0;
    if (origin<0) return origin;
    // Test before addition, including INT64_MIN/MAX, to avoid signed overflow.
    if (offset<-(int64_t)origin) return LFS_ERR_INVAL;
    if (offset>(int64_t)0x7FFFFFFF-origin) return Overflow;
    return lfs_file_seek(&filesystem,&slot->file,(int32_t)(origin+offset),LFS_SEEK_SET);
}
int32_t FileStore::Size(uint32_t owner,uint32_t handle) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);
    return slot?lfs_file_size(&filesystem,&slot->file):LFS_ERR_BADF;
}
int32_t FileStore::TruncateFile(uint32_t owner,uint32_t handle,uint32_t size) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);
    if (!slot||slot->mode==Read) return LFS_ERR_BADF;
    if (readOnly) return ReadOnly;
    if (size>0x7FFFFFFFU) return LFS_ERR_FBIG;
    return lfs_file_truncate(&filesystem,&slot->file,size);
}
int32_t FileStore::Sync(uint32_t owner,uint32_t handle) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_REG);
    return slot?lfs_file_sync(&filesystem,&slot->file):LFS_ERR_BADF;
}
void FileStore::CopyInfo(Info& to,const lfs_info& from) {
    to.type=from.type;to.size=from.size;gtos_lfs_memcpy(to.name,from.name,sizeof(to.name));
}
int32_t FileStore::ReadDirectory(uint32_t owner,uint32_t handle,Info& info) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_DIR);if (!slot) return LFS_ERR_BADF;
    lfs_info value={};int32_t result=lfs_dir_read(&filesystem,&slot->directory,&value);
    if (result>0) CopyInfo(info,value);
    return result;
}
int32_t FileStore::RewindDirectory(uint32_t owner,uint32_t handle) {
    InterruptGuard guard;Slot* slot=Find(owner,handle,LFS_TYPE_DIR);
    return slot?lfs_dir_rewind(&filesystem,&slot->directory):LFS_ERR_BADF;
}
int32_t FileStore::Stat(const char* path,Info& info) {
    InterruptGuard guard;if (!mounted) return LFS_ERR_BADF;
    int32_t error=ValidPath(path);if (error) return error;
    lfs_info value={};error=lfs_stat(&filesystem,path,&value);if (!error) CopyInfo(info,value);return error;
}
int32_t FileStore::MakeDirectory(const char* path) {
    InterruptGuard guard;if (!mounted) return LFS_ERR_BADF;
    int32_t error=ValidPath(path);if (error) return error;
    if (readOnly) return ReadOnly;
    return lfs_mkdir(&filesystem,path);
}
int32_t FileStore::Remove(const char* path) {
    InterruptGuard guard;if (!mounted) return LFS_ERR_BADF;
    int32_t error=ValidPath(path);if (error) return error;
    if (readOnly) return ReadOnly;
    return lfs_remove(&filesystem,path);
}
int32_t FileStore::Rename(const char* from,const char* to) {
    InterruptGuard guard;if (!mounted) return LFS_ERR_BADF;
    int32_t error=ValidPath(from);if (error) return error;
    error=ValidPath(to);if (error) return error;
    if (readOnly) return ReadOnly;
    return lfs_rename(&filesystem,from,to);
}
int32_t FileStore::HandleInfo(uint32_t owner,uint32_t handle,Info& info) {
    InterruptGuard guard;Slot* slot=Find(owner,handle);if (!slot) return LFS_ERR_BADF;
    const int32_t size=slot->kind==LFS_TYPE_REG?lfs_file_size(&filesystem,&slot->file):0;
    if (size<0) return size;
    Info value={};value.type=slot->kind;value.size=size;info=value;return 0;
}
