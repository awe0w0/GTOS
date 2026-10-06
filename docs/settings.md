# Desktop settings v1

`SettingsStore` persists the desktop language and theme in an optional, bounded
extension of a dedicated `GTSTOR1` application image. It is not a new filesystem,
partition format, disk formatter, or general configuration database. The first
supported locales are English and Simplified Chinese; themes are dark and light.
Defaults are English/dark. No disk is needed to use those settings for a session.

## Image reservation and compatibility

The existing application-store format and geometry are **unchanged**:

| Sectors | Owner |
| --- | --- |
| 0–258 inclusive | Existing `GTSTOR1` superblock, directories and app payloads |
| 259 and 260 | Optional dual-copy desktop settings extension |
| 261 onward | Unclaimed; neither settings nor app-store writes here |

The minimum capacity for this extension is 261 × 512 = 133,632 bytes.
The superblock's application-store length remains **259**, not 261. Existing
259- or 260-sector images remain usable by `AppStore`; settings are unavailable
and no image is extended automatically. The existing `tools/disk.py create`
command creates a new 8 MiB image by default, with both settings sectors already
zero-filled. It needs no migration, formatting change, or tool modification.

```sh
python3 tools/disk.py create gtos-data.img --size-mib 8
```

The image creator continues to refuse existing files and devices. The kernel
does not format disks. Never attach a valuable physical disk to this experimental
OS, and never run host mutations against an image attached to a running VM.

`SettingsStore` uses the actual `AppStore::Mount()` validator before reading its
extension and again before each changed save or post-load durability barrier.
The superblock must have the exact
v1 geometry, reserved bytes and CRC, and at least one valid app directory must be
available. Either settings sector containing unrecognized nonzero data disables
**all** settings writes. Such data is never erased or "initialized". An all-zero
sector is the only blank representation; an all-`0xFF` sector is unknown data.
Recognized but unsupported versions, values, lengths, or nonzero extensions are
also preserved read-only, even when their CRC is invalid.

The app-store kernel code and current offline image tools stay bounded to
sectors 0–258. App install, replace and uninstall preserve both settings records
and the rest of the tail. Older kernels/tools can continue using those apps;
they simply do not expose the settings extension.

## Record layout

Each copy occupies one 512-byte sector. All integers are unsigned little-endian.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | Exact magic `GTSET01\0` |
| 8 | 4 | Format version, 1 |
| 12 | 4 | Record length, 512 |
| 16 | 4 | Generation counter |
| 20 | 4 | Locale: 0 = English, 1 = Simplified Chinese |
| 24 | 4 | Theme: 0 = dark, 1 = light |
| 28 | 480 | Reserved; all zero |
| 508 | 4 | Existing GTOS CRC32 over bytes 0–507 |

A known v1 record with the supported header, supported values, zero reserved
bytes and a bad CRC is a damaged settings record. It is not accepted as a value,
but its sector may be overwritten when another valid settings copy survives.
A damaged header, unknown value or extension is conservatively read-only.
If no valid copy survives and either sector is nonblank, the store defaults
read-only; it never silently resets the damaged area. A user can still change
the session settings without a successful persistent save.

## Commit, recovery and generation rules

Loading is read-only. Two blank sectors yield writable English/dark defaults.
Saving those unchanged defaults does not allocate a record. Supported changed
values write the inactive copy at the next generation, flush the device, and
compare a complete readback byte-for-byte. Only then do `Current()` and the
active generation change. A first save uses sector 259 at generation 1;
subsequent saves alternate sectors. A save identical to `Current()` issues no
sector writes. Blank defaults and repeated unchanged values whose durability
this instance already established also need no flush. The first save after
loading a record requires the durability barrier described below, including
when the requested value is unchanged. Invalid enum values never reach the
device; both enums have explicit `uint32_t` storage so rejecting arbitrary
input values is defined behavior.

Both settings sectors are reread and compared with their loaded copies before
every changed save and before a post-load durability barrier. If another writer
or media change has changed either one,
saving stops before mutation with `MediaChanged`. App-directory changes remain
allowed because the app store is independently validated. This is a synchronous
single-writer API; it does not provide cross-core locking, hot-swap synchronization
or protection against another writer racing between the check and the write.

The generation counter wraps modulo 2^32. For two valid records, compute
`delta = generation1 - generation0` as unsigned 32-bit arithmetic:

- `0 < delta < 0x80000000`: copy 1 is newer
- `delta > 0x80000000`: copy 0 is newer
- `delta == 0`: accept only identical records
- `delta == 0x80000000`: ambiguous; default read-only, without guessing

Therefore generation 0 follows `0xFFFFFFFF`. The protocol's successive writes
leave adjacent generations; the serial comparison assumes genuine updates
have not diverged by half of the counter range. Different records with equal
generations and exact-half-range pairs are rejected without writes.

An interrupted update never writes over the last valid active copy. CRC failures
in a recognized inactive record fall back to the previous valid settings. Torn
headers can force read-only mode while preserving the other readable copy.
The first write may fail before any persistent settings exist; defaults remain
available, and a nonblank damaged/unknown area is preserved rather than reset.

Failed writes, flushes or readback leave live `Current()` at its last loaded or
acknowledged value and put the store in `Faulted`; another `Load()` is required
before writes.
A failed flush/readback can still have left a complete newer record durable.
After remount, either the old value or that complete, valid newer value may be
selected. Save failure is not a guarantee that the disk is unchanged. The old
valid copy remains intact until a later successful save replaces it.

### Reloading is not a durability proof

Device reads can return dirty cached sectors. In particular, a `Load()` without
a power cycle can select a complete generation whose earlier flush failed.
That is true for a fresh `SettingsStore` instance as well as the instance that
observed the failure. Reclaiming the other sector at this point could destroy
the only genuinely durable copy.

Every load therefore marks a selected record's durability as unknown. Before
the next `Save()` may overwrite its companion **or acknowledge an unchanged
value**, the store revalidates ownership and both sectors, then requires a
successful `Flush()`. This establishes durability of the selected record before
reusing the other one. Barrier failure enters `Faulted`, returns false, and
issues no sector writes. A successful barrier or commit establishes durability
for that instance until another load or I/O fault. Loading itself remains
read-only; it never flushes. `StatusText()` says "Settings loaded" until durability
has been established, and "Settings saved" afterward.

This adds at most one recovery barrier per successful load of an existing
record. An unchanged recovered save flushes without writing either sector;
later unchanged saves do neither. Blank default settings require no recovery
barrier because no prior copy will be reclaimed or acknowledged as persisted.

CRC32 detects accidental corruption; it does not authenticate a disk or defend
against malicious edits or checksum collisions. This design cannot guarantee
recovery from corruption of both copies, dishonest hardware flushes, unrelated
media damage, or concurrent writers. It does not repair the application store.

## Kernel and desktop API

```cpp
gtos::storage::SettingsStore settings(&disk);
settings.Load(); // Always initializes Current(); check Writable()/Status().
// Pass &settings to the desktop after Load(), including a read-only store.

gtos::storage::Settings chosen = settings.Current();
chosen.locale = gtos::storage::SimplifiedChinese;
chosen.theme = gtos::storage::Light;
bool saved = settings.Save(chosen);
```

The storage enums deliberately do not depend on GUI or localization headers.
Map supported locale values explicitly to `i18n::Locale`; do not serialize a
translated display string. The desktop may apply a language/theme immediately
for the current session, then call `Save`. On failure, retain that session choice
and clearly report that it was not saved; `Current()` still reports the last
loaded or acknowledged value, not the failed save's UI choice.

- `Load()` returns true precisely when the result is writable. Always consult
  `Current()`, even if false: it may contain a valid read-only persisted copy
- `Current()` supplies English/dark defaults whenever no unambiguous supported
  record can be recovered
- `HasPersistedSettings()` distinguishes an accepted disk record from defaults;
  a read can come from device cache, so it does not replace the recovery barrier
- `Status()` is `Unloaded`, `Ready`, `ReadOnly`, `Unavailable`, or `Faulted`
- `Writable()` is true only in `Ready`; `LastError()` gives the detailed reason
- `StatusText()` supplies an English diagnostic; the desktop can localize errors
- `Save()` never writes sectors for an unchanged value; it can require a recovery
  flush on a ready store after loading. False does not erase or roll back the
  desktop's independently applied session values

`Ready` includes a blank, safely writable extension or recovery from one known
bad-CRC companion. `ReadOnly` covers unknown/unsupported data, ambiguous settings,
unrecoverable settings corruption, app-store metadata corruption, or a changed
settings area. `Unavailable` covers a missing, unowned or too-small device.
`Faulted` represents an I/O error. No state triggers formatting or automatic
repair. `LastError()` describes the latest operation, so an invalid input error
can coexist with `Ready` and a later valid save may still succeed.

## Verification

```sh
tests/settings_test.sh
tests/settings_sanitizer_test.sh
python3 -m unittest discover -s tests -p 'settings_tool_test.py'
tests/storage_test.sh
python3 -m unittest discover -s tests -p 'storage_tool_test.py'
```

The C++ runner compiles the actual kernel sources for i386, using `qemu-i386` or
native execution when supported, and otherwise compiles/runs those same sources
as x86_64. Its fault-injecting `BlockDevice` has separate cached and durable
buffers and simulates power loss. Coverage includes defaults, unchanged saves,
supported values, all insufficient capacities, both unknown-sector positions,
future/unsupported fields, owner validation, changed-media refusal, app-store
coexistence, CRC recovery, conflicting generations, rollover in both copy
orientations, **2,052** torn updates (all 513 prefix lengths for a blank inactive
copy, a previously valid inactive copy, and same-instance/fresh-instance reloads
without discarding dirty cache), interrupted first saves, both failed-flush
durability outcomes, failed/corrupt readback,
a short write falsely reported as successful, and remount persistence.
Recovery-specific checks also cover unchanged saves, failed recovery barriers
with either durability outcome, and changed ownership/sectors or failed reads
before the barrier. The hosted sanitizer runner uses strict ASan/UBSan with
`UBSAN_OPTIONS=halt_on_error=1` and
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` against the same C++ test cases.

The Python checks use the unchanged real `tools/disk.py` implementation to verify
default image capacity/blank reservation, full app capacity and removal without
touching settings or unknown trailing bytes, app-tool write bounds, and continued
support for old 259-sector images. These are deterministic tests; they do not
alone claim a physical-ATA or full-desktop reboot test has passed.
