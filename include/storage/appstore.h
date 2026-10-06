#ifndef GTOS_STORAGE_APPSTORE_H
#define GTOS_STORAGE_APPSTORE_H
#include <storage/blockdevice.h>
#include <apps/package.h>
namespace gtos { namespace storage {
struct AppInfo { char id[24], title[24]; uint32_t slot, length, checksum; };
// Dedicated raw-image format, not a general-purpose filesystem or partition parser.
class AppStore {
public:
    enum { MaxApps=8, SlotCount=16, SlotSectors=16, DataStart=3, TotalSectors=259 };
    enum Error { OK, NotMounted, NoDisk, IOFailure, NotFormatted, Corrupt,
        Full, NotFound, InvalidPackage, BufferSmall };
private:
    BlockDevice* disk;
    bool mounted, valid[2];
    uint8_t dirs[2][512];
    uint32_t active, generation, count;
    AppInfo entries[MaxApps];
    Error status;
    bool ValidDirectory(const uint8_t* sector) const;
    void DecodeDirectory();
    bool Commit(const AppInfo* next,uint32_t nextCount);
    bool Fail(Error e);
public:
    explicit AppStore(BlockDevice* disk);
    bool Mount();
    bool Install(const uint8_t* package,uint32_t length);
    bool Uninstall(const char* id);
    bool Read(const char* id,uint8_t* out,uint32_t capacity,uint32_t* length);
    bool Mounted() const { return mounted; }
    uint32_t Count() const { return count; }
    const AppInfo* Get(uint32_t index) const { return index<count?&entries[index]:0; }
    Error Status() const { return status; }
    const char* StatusText() const;
    uint32_t Generation() const { return generation; }
};
} }
#endif
