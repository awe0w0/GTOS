#include <storage/gpt.h>

using namespace gtos::storage;

namespace {
    void Print(const char* text) {
        uint32_t length = 0, result;
        while (text[length]) ++length;
        asm volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(text), "d"(length) : "memory");
    }

    uint32_t failures = 0;

    void Check(bool condition, const char* name) {
        Print(condition ? "ok " : "FAIL ");
        Print(name);
        Print("\n");
        if (!condition) ++failures;
    }

    void Put(uint8_t* destination, uint64_t value, uint32_t bytes) {
        for (uint32_t i = 0; i < bytes; ++i) {
            destination[i] = (uint8_t)value;
            value >>= 8;
        }
    }

    struct Layout {
        uint32_t sector;
        uint64_t sectors, partition;
        uint32_t entriesCrc, primaryCrc, backupCrc;
    };

    // CRCs of the complete 16 KiB entry array and the two 92-byte headers.
    const Layout Wide = {512, 0x1D1C11C00ULL, 0x100000800ULL,
                         0xA7CD9F20, 0x1D1F484C, 0xB36DF9DB};
    const Layout FourK = {4096, 0x20000, 0x800, 0x3D10B514, 0xF3BD4129, 0x29A036BE};
    const Layout BadPartition = {512, Wide.sectors, Wide.sectors,
                                 0xFAE9F2CE, 0x8F82EADB, 0x21F05B4C};

    class Disk : public ReadableDisk {
        const Layout& layout;
        uint32_t TableSectors() const { return 16384 / layout.sector; }
        uint64_t BackupTable() const { return layout.sectors - TableSectors() - 1; }

        void Header(bool backup, uint8_t* block) const {
            Put(block, 0x5452415020494645ULL, 8);
            Put(block + 8, 0x10000, 4);
            Put(block + 12, 92, 4);
            Put(block + 16, backup ? layout.backupCrc : layout.primaryCrc, 4);
            Put(block + 24, backup ? layout.sectors - 1 : 1, 8);
            Put(block + 32, backup ? 1 : layout.sectors - 1, 8);
            Put(block + 40, TableSectors() + 2, 8);
            Put(block + 48, BackupTable() - 1, 8);
            block[56] = 0x77;
            Put(block + 72, backup ? BackupTable() : 2, 8);
            Put(block + 80, 128, 4);
            Put(block + 84, 128, 4);
            Put(block + 88, layout.entriesCrc, 4);
            if (backup ? badBackup : badPrimary) block[16] ^= 1;
        }

        void Entry(bool backup, uint8_t* block) const {
            block[0] = backup || !badArray ? 0x28 : 0x29;
            block[15] = 0x21;
            block[16] = 0xDE;
            block[31] = 0x19;
            Put(block + 32, layout.partition, 8);
            Put(block + 40, layout.partition + 4095, 8);
            Put(block + 48, 1ULL << 60, 8);
            Put(block + 56, 0x78C1, 2);
            Put(block + 58, 'G', 2);
        }
    public:
        bool badPrimary, badBackup, badArray, hybrid, unexpectedRead;

        explicit Disk(const Layout& value)
            : layout(value), badPrimary(false), badBackup(false), badArray(false),
              hybrid(false), unexpectedRead(false) {}

        uint32_t LogicalSectorBytes() const override { return layout.sector; }
        uint64_t LogicalSectorCount() const override { return layout.sectors; }

        bool ReadSectors(uint64_t address, uint32_t count, uint8_t* block) override {
            if (count != 1 || address >= layout.sectors) {
                unexpectedRead = true;
                return false;
            }
            for (uint32_t i = 0; i < layout.sector; ++i) block[i] = 0;
            if (!address) {
                block[510] = 0x55;
                block[511] = 0xAA;
                block[450] = 0xEE;
                Put(block + 454, 1, 4);
                Put(block + 458, layout.sectors > 0x100000000ULL ? 0xFFFFFFFFu : layout.sectors - 1, 4);
                if (hybrid) block[466] = 0x07;
            } else if (address == 1 || address == layout.sectors - 1) {
                Header(address != 1, block);
            } else if (address == 2 || address == BackupTable()) {
                Entry(address != 2, block);
            } else if (address >= 2 + TableSectors() && address < BackupTable()) {
                unexpectedRead = true;
                return false;
            }
            return true;
        }
    };
}

extern "C" void __cxa_pure_virtual() { asm volatile("ud2"); }

extern "C" int gptTests() {
    GptTable table;
    GptPartition entry;
    Disk wide(Wide);
    Check(table.Open(wide) && !table.UsingBackup() && table.EntryCount() == 128,
          "large 512-byte-sector disk opens from its primary table");
    Check(table.Entry(0, entry) == GptPartitionFound && entry.first == Wide.partition &&
          entry.last == Wide.partition + 4095 && entry.attributes == (1ULL << 60) &&
          entry.type[0] == 0x28 && entry.type[15] == 0x21 && entry.id[0] == 0xDE &&
          entry.id[15] == 0x19 && entry.name[0] == 0x78C1 && entry.name[1] == 'G' &&
          entry.name[2] == 0, "partition keeps its high LBA bits, GUIDs and UTF-16 name");
    Check(table.Entry(127, entry) == GptUnused && table.Entry(128, entry) == GptEntryError,
          "empty entry and index outside table are distinct");
    wide.badPrimary = true;
    Check(table.Open(wide) && table.UsingBackup() &&
          table.Entry(0, entry) == GptPartitionFound && entry.first == Wide.partition,
          "invalid primary header CRC falls back to high-LBA backup");
    wide.badPrimary = false;
    wide.badArray = true;
    Check(table.Open(wide) && table.UsingBackup(), "invalid primary array CRC falls back to backup");
    wide.badBackup = true;
    Check(!table.Open(wide) && !table.EntryCount() && table.Entry(0, entry) == GptEntryError,
          "two invalid copies clear any previous table");
    wide.hybrid = true;
    wide.badArray = wide.badBackup = false;
    Check(!table.Open(wide), "hybrid MBR is refused");
    Disk fourK(FourK);
    Check(table.Open(fourK) && table.Entry(0, entry) == GptPartitionFound &&
          entry.first == FourK.partition && entry.last == FourK.partition + 4095,
          "4 KiB sector table opens and reads the correct slot");
    Disk bad(BadPartition);
    Check(table.Open(bad) && table.Entry(0, entry) == GptEntryError,
          "valid table CRC does not authorize an out-of-range partition");
    Check(!wide.unexpectedRead && !fourK.unexpectedRead && !bad.unexpectedRead,
          "reads are confined to GPT metadata");
    Print(failures ? "GPT tests failed\n" : "GPT tests passed\n");
    return failures ? 1 : 0;
}

asm(".global _start\n_start:\n xorl %ebp,%ebp\n andl $-16,%esp\n call gptTests\n"
    " movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
