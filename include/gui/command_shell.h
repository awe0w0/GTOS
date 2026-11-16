#ifndef GTOS_GUI_COMMAND_SHELL_H
#define GTOS_GUI_COMMAND_SHELL_H

#include <storage/ntfs.h>

namespace gtos { namespace gui {
    class CommandShell {
    public:
        static const uint32_t InputBytes = 160;
    private:
        static const uint32_t Rows = 64, Columns = 96, MaxDepth = 12;
        char output[Rows][Columns + 1];
        char input[InputBytes], history[InputBytes];
        uint32_t lastRow, rowCount, column, scroll, length, cursor;
        storage::ReadableDisk* disk;
        storage::GptTable partitions;
        storage::NtfsVolume volume;
        storage::NtfsEntry directories[MaxDepth];
        uint32_t mounted, depth;

        void Clear();
        void NewLine();
        void Write(const char* text);
        void Hex(uint64_t value);
        void Number(uint32_t value);
        void Error(storage::NtfsStatus status);
        void Execute(char* line);
        void ListPartitions();
        void Mount(const char* argument);
        void ListDirectory();
        void ChangeDirectory(const char* argument);
        void Cat(const char* argument);

    public:
        CommandShell();
        bool AttachDisk(storage::ReadableDisk& source);
        void Key(uint8_t key);
        const char* Line(uint32_t rowsFromBottom) const;
        uint32_t LineCount() const { return rowCount - scroll; }
        const char* Input() const { return input; }
        uint32_t Cursor() const { return cursor; }
    };
} }

#endif
