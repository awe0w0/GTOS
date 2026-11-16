#include <gui/command_shell.h>
#include <common/version.h>
#include <drivers/keyboard.h>

using namespace gtos::gui;
using namespace gtos::storage;

namespace {
    bool Equal(const char* left, const char* right) {
        while (*left && *left == *right) { ++left; ++right; }
        return *left == *right;
    }

    uint32_t Length(const char* text) {
        uint32_t count = 0;
        while (text[count]) ++count;
        return count;
    }

    void Copy(char* output, const char* text) {
        while ((*output++ = *text++)) {}
    }
}

CommandShell::CommandShell()
    : lastRow(0), rowCount(1), column(0), scroll(0), length(0), cursor(0),
      disk(0), mounted(0), depth(0) {
    output[0][0] = input[0] = history[0] = 0;
    directories[0].id = NtfsVolume::Root();
    Write("GTOS Shell\nType help for commands.\n");
}

bool CommandShell::AttachDisk(ReadableDisk& source) {
    mounted = depth = 0;
    disk = partitions.Open(source) ? &source : 0;
    return disk != 0;
}

void CommandShell::Clear() {
    lastRow = column = scroll = 0;
    rowCount = 1;
    output[0][0] = 0;
}

void CommandShell::NewLine() {
    lastRow = (lastRow + 1) % Rows;
    if (rowCount < Rows) ++rowCount;
    column = 0;
    output[lastRow][0] = 0;
}

void CommandShell::Write(const char* text) {
    while (*text) {
        const uint8_t code = (uint8_t)*text;
        if (code == '\n') {
            NewLine();
            ++text;
            continue;
        }
        uint32_t count = 1;
        if (code >= 0xC2 && code <= 0xF4) {
            const uint32_t expected = code < 0xE0 ? 2 : code < 0xF0 ? 3 : 4;
            while (count < expected && ((uint8_t)text[count] & 0xC0) == 0x80) ++count;
        }
        if (count > Columns - column) NewLine();
        if (code < 32 || code == 127) {
            output[lastRow][column++] = '.';
            ++text;
        } else {
            while (count--) output[lastRow][column++] = *text++;
        }
        output[lastRow][column] = 0;
    }
}

void CommandShell::Hex(uint64_t value) {
    char text[19];
    text[0] = '0'; text[1] = 'x'; text[18] = 0;
    for (uint32_t i = 18; i > 2; --i) {
        text[i - 1] = "0123456789ABCDEF"[value & 15];
        value >>= 4;
    }
    Write(text);
}

void CommandShell::Number(uint32_t value) {
    char text[11];
    uint32_t at = 10;
    text[at] = 0;
    do { text[--at] = (char)('0' + value % 10); value /= 10; } while (value);
    Write(text + at);
}

void CommandShell::Error(NtfsStatus status) {
    switch (status) {
        case NtfsNotMounted: Write("Select a partition with mount.\n"); break;
        case NtfsIo: Write("Disk read failed.\n"); break;
        case NtfsInvalid: Write("Invalid volume or file type.\n"); break;
        case NtfsUnsupported: Write("Feature unsupported by the read-only NTFS driver.\n"); break;
        case NtfsNotFound: Write("File or directory not found.\n"); break;
        case NtfsOk: break;
    }
}

void CommandShell::ListPartitions() {
    if (!disk) { Write("No GPT disk attached.\n"); return; }
    for (uint32_t i = 0; i < partitions.EntryCount(); ++i) {
        GptPartition partition;
        const GptEntryResult status = partitions.Entry(i, partition);
        if (status == GptUnused) continue;
        if (status == GptEntryError) { Write("Invalid GPT entry.\n"); return; }
        Number(i + 1);
        Write(mounted == i + 1 ? " * " : "   ");
        Hex(partition.first); Write(" - "); Hex(partition.last); Write("\n");
    }
}

void CommandShell::Mount(const char* argument) {
    if (!disk) { Write("No GPT disk attached.\n"); return; }
    uint32_t index = 0;
    for (; *argument >= '0' && *argument <= '9' && index < 100000000; ++argument)
        index = index * 10 + (*argument - '0');
    if (*argument || !index || index > partitions.EntryCount()) {
        Write("Usage: mount <partition number>\n");
        return;
    }
    GptPartition partition;
    if (partitions.Entry(index - 1, partition) != GptPartitionFound) {
        Write("Partition unavailable.\n");
        return;
    }
    mounted = depth = 0;
    const NtfsStatus status = volume.Mount(*disk, partition);
    if (status != NtfsOk) { Error(status); return; }
    mounted = index;
    Write("Mounted read-only partition "); Number(index); Write("\n");
}

void CommandShell::ListDirectory() {
    if (!mounted) { Error(NtfsNotMounted); return; }
    Error(volume.List(directories[depth].id, [](const NtfsEntry& entry, void* context) {
        CommandShell& shell = *(CommandShell*)context;
        shell.Write(entry.name);
        if (entry.directory) shell.Write("/");
        shell.Write("\n");
        return true;
    }, this));
}

void CommandShell::ChangeDirectory(const char* argument) {
    if (!mounted) { Error(NtfsNotMounted); return; }
    if (Equal(argument, "/")) { depth = 0; return; }
    if (Equal(argument, "..")) { if (depth) --depth; return; }
    if (Equal(argument, ".")) return;
    if (!argument[0]) { Write("Usage: cd <directory>\n"); return; }
    if (depth + 1 == MaxDepth) { Write("Directory depth limit reached.\n"); return; }
    NtfsEntry& target = directories[depth + 1];
    const NtfsStatus status = volume.Find(directories[depth].id, argument, target);
    if (status != NtfsOk) { Error(status); return; }
    if (!target.directory || target.reparse) { Error(NtfsInvalid); return; }
    ++depth;
}

void CommandShell::Cat(const char* argument) {
    if (!mounted) { Error(NtfsNotMounted); return; }
    NtfsEntry file;
    const NtfsStatus found = volume.Find(directories[depth].id, argument, file);
    if (found != NtfsOk) { Error(found); return; }
    if (file.directory || file.reparse) { Error(NtfsInvalid); return; }
    char text[2049];
    uint32_t bytes;
    const NtfsStatus status = volume.ReadFile(file.id, 0, sizeof(text) - 1, (uint8_t*)text, bytes);
    if (status != NtfsOk) { Error(status); return; }
    uint32_t visible = 0;
    for (uint32_t i = 0; i < bytes; ++i)
        if (text[i] != '\r') text[visible++] = text[i] == 0 ? '.' : text[i];
    text[visible] = 0;
    Write(text);
    if (column) Write("\n");
    if (file.bytes > bytes) Write("Preview limited to 2048 bytes.\n");
}

void CommandShell::Execute(char* line) {
    while (*line == ' ') ++line;
    if (!*line) return;
    char* argument = line;
    while (*argument && *argument != ' ') ++argument;
    if (*argument) {
        *argument++ = 0;
        while (*argument == ' ') ++argument;
    }

    if (Equal(line, "help")) {
        Write("help  clear  version  echo <text>\n");
        Write("disks  partitions  mount <number>\n");
        Write("ls  pwd  cd <name|..|/>  cat <name>\n");
        Write("Up/Down recall input. Page Up/Down scroll output.\n");
    } else if (Equal(line, "clear")) Clear();
    else if (Equal(line, "version")) Write("GTOS " GTOS_VERSION_RELEASE "\n");
    else if (Equal(line, "echo")) { Write(argument); Write("\n"); }
    else if (Equal(line, "disks")) {
        if (!disk) Write("No GPT disk attached.\n");
        else {
            Write("disk 0  GPT  read-only  sectors ");
            Hex(disk->LogicalSectorCount()); Write("\n");
            Write("Logical sector bytes "); Number(disk->LogicalSectorBytes()); Write("\n");
        }
    } else if (Equal(line, "partitions")) ListPartitions();
    else if (Equal(line, "mount")) Mount(argument);
    else if (Equal(line, "ls")) ListDirectory();
    else if (Equal(line, "cd")) ChangeDirectory(argument);
    else if (Equal(line, "cat")) Cat(argument);
    else if (Equal(line, "pwd")) {
        if (!mounted) Error(NtfsNotMounted);
        else {
            Write("/");
            for (uint32_t i = 1; i <= depth; ++i) {
                Write(directories[i].name); Write("/");
            }
            Write("\n");
        }
    } else Write("Unknown command. Type help.\n");
}

void CommandShell::Key(uint8_t key) {
    using namespace gtos::drivers;
    if (key == KeyPageUp) {
        scroll = scroll + 8 < rowCount ? scroll + 8 : rowCount - 1;
    } else if (key == KeyPageDown) {
        scroll = scroll >= 8 ? scroll - 8 : 0;
    } else if (key == KeyHome) cursor = 0;
    else if (key == KeyEnd) cursor = length;
    else if (key == 0x81) { if (cursor) --cursor; }
    else if (key == 0x82) { if (cursor < length) ++cursor; }
    else if (key == 0x83 || key == 0x84) {
        Copy(input, key == 0x83 ? history : "");
        cursor = length = Length(input);
    } else if (key == '\n') {
        scroll = 0;
        if (length) {
            Copy(history, input);
            Write("> "); Write(input); Write("\n");
            Execute(input);
        }
        input[0] = 0; length = cursor = 0;
    } else if (key == '\b' && cursor) {
        for (uint32_t i = cursor; i <= length; ++i) input[i - 1] = input[i];
        --cursor; --length;
    } else if (key >= 32 && key < 127 && length + 1 < sizeof(input)) {
        for (uint32_t i = length + 1; i > cursor; --i) input[i] = input[i - 1];
        input[cursor++] = (char)key;
        ++length;
    }
}

const char* CommandShell::Line(uint32_t rowsFromBottom) const {
    const uint32_t offset = rowsFromBottom + scroll;
    return offset < rowCount ? output[(lastRow + Rows - offset) % Rows] : "";
}
