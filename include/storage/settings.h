#ifndef GTOS_STORAGE_SETTINGS_H
#define GTOS_STORAGE_SETTINGS_H
#include <storage/blockdevice.h>

namespace gtos { namespace storage {

// Stable on-disk values; translate explicitly at UI/localization boundaries.
enum Locale : uint32_t { English=0, SimplifiedChinese=1 };
enum Theme : uint32_t { Dark=0, Light=1 };
struct Settings { Locale locale; Theme theme; };

// Optional extension of a validated dedicated GTSTOR1 image, never a formatter.
// Single writer, synchronous BlockDevice access; see docs/settings.md.
class SettingsStore {
public:
    enum { FirstSector=259, CopyCount=2, RequiredSectors=261, FormatVersion=1 };
    enum State { Unloaded, Ready, ReadOnly, Unavailable, Faulted };
    enum Error { NoError, NotLoaded, NoDisk, InsufficientCapacity, UnownedDisk,
        CorruptAppStore, IOFailure, UnknownData, UnsupportedRecord,
        CorruptRecords, AmbiguousGeneration, InvalidSettings, MediaChanged };

private:
    BlockDevice* disk;
    Settings current;
    State state;
    Error error;
    uint8_t copies[CopyCount][512];
    uint32_t active, generation;
    bool hasRecord, durabilityKnown;

    bool Fail(State next,Error reason);
    bool CheckOwner();

public:
    explicit SettingsStore(BlockDevice* disk);
    // Read only: always initializes Current(), defaulting to English/Dark.
    // True means writable. False may still leave a valid read-only Current().
    bool Load();
    // Writes only changed, supported values; failure requires checking Status().
    // First Save after loading a record establishes a flush barrier even when
    // unchanged, because readback alone cannot prove device-cache durability.
    // After uncertain I/O, Load() is required before another write.
    bool Save(const Settings& settings);
    const Settings& Current() const { return current; }
    bool Writable() const { return state==Ready; }
    bool HasPersistedSettings() const { return hasRecord; }
    State Status() const { return state; }
    Error LastError() const { return error; }
    uint32_t Generation() const { return generation; }
    const char* StatusText() const;
};

} }
#endif
