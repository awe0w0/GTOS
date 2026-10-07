// Independent retained-stream oracle for the real allocation-free boot ring.
#include <common/boot_log.h>
extern "C" int printf(const char*, ...);
using gtos::common::BootLog;
static uint32_t checks = 0, failures = 0;
static char history[65536];
static uint32_t total = 0;
static void Check(bool value, const char* detail) {
    ++checks;
    if (!value) { ++failures; printf("FAIL %s\n", detail); }
}
static void Reset() { BootLog::Reset(); total = 0; }
static void Append(char c) {
    BootLog::Put(c);
    if (c) history[total++] = c;
}
static bool Equal(const char* a, const char* b) {
    for (uint32_t i = 0; i < 512; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}
static void Oracle() {
    uint32_t start = total > BootLog::Capacity ? total - BootLog::Capacity : 0;
    const uint32_t bytes = total - start;
    uint32_t lines = 0;
    for (uint32_t i = start; i < total; ++i) if (history[i] == '\n') ++lines;
    if (bytes && history[total - 1] != '\n') ++lines;
    Check(BootLog::Bytes() == bytes, "retained byte oracle");
    Check(BootLog::Dropped() == start, "overwritten byte oracle");
    Check(BootLog::Lines() == lines, "line count oracle");
    uint32_t cursor = start;
    for (uint32_t line = 0; line < lines; ++line) {
        uint32_t end = cursor;
        while (end < total && history[end] != '\n') ++end;
        const uint32_t capacities[] = {1, 2, 7, 96, 512};
        for (uint32_t k = 0; k < 5; ++k) {
            char actual[514], expected[512];
            for (uint32_t i = 0; i < sizeof(actual); ++i) actual[i] = '~';
            uint32_t count = end - cursor;
            if (count >= capacities[k]) count = capacities[k] - 1;
            for (uint32_t i = 0; i < count; ++i) expected[i] = history[cursor + i];
            expected[count] = 0;
            Check(BootLog::ReadLine(line, actual + 1, capacities[k]), "valid retained line");
            Check(Equal(actual + 1, expected), "line/truncation oracle");
            Check(actual[0] == '~' && actual[capacities[k] + 1] == '~', "destination bounds");
        }
        cursor = end + 1;
    }
    char invalid[2] = {'x', 'y'};
    Check(!BootLog::ReadLine(lines, invalid, sizeof(invalid)) && invalid[0] == 0 &&
          invalid[1] == 'y', "out of range writes only NUL");
    invalid[0] = 'x';
    Check(!BootLog::ReadLine(0, invalid, 0) && invalid[0] == 'x', "zero capacity unchanged");
    Check(!BootLog::ReadLine(0, 0, 1), "null destination rejected");
    Check(!BootLog::ReadLine(0xFFFFFFFFU, invalid, 1), "largest index rejected");
}
int main() {
    Reset(); Oracle();
    Append(0); Oracle();
    const char sample[] = "B01 HANDOFF\n\nB02 MEMORY\nunfinished";
    for (uint32_t i = 0; sample[i]; ++i) Append(sample[i]);
    Oracle(); Append('\n'); Oracle(); Append('\n'); Oracle();
    Reset();
    for (uint32_t i = 0; i < BootLog::Capacity; ++i) Append('\n');
    Oracle(); Append('X'); Oracle(); Append('\n'); Oracle();
    Reset();
    for (uint32_t i = 0; i < BootLog::Capacity * 2 + 17; ++i) Append('A' + i % 26);
    Oracle(); Append('\n'); Oracle();
    Reset();
    uint32_t seed = 0xA72D905BU;
    for (uint32_t i = 0; i < BootLog::Capacity * 4 + 103; ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        Append(seed % 13 == 0 ? '\n' : (char)(' ' + seed % 95));
        if (i % 1733 == 0 || i == BootLog::Capacity - 1 || i == BootLog::Capacity)
            Oracle();
    }
    Oracle(); Reset(); Oracle();
    printf("BOOT LOG HOST %s checks=%u capacity=%u\n", failures ? "FAIL" : "PASS",
           checks, (uint32_t)BootLog::Capacity);
    return failures ? 1 : 0;
}
