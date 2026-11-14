// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <cerrno>
#include <future>
#include <stdexcept>
#include <string>
#include <system_error>

#include <common/version.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <process/native_window_adapter.h>
#include <process/native_window_frame.h>
#include <stdio.h>
#include <storage/content_volume_layout.h>
#include <unistd.h>

#include "base/containers/span.h"
#include "base/cpu.h"
#include "base/files/drive_info.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/process/process.h"
#include "base/process/process_metrics.h"
#include "base/system/sys_info.h"
#if defined(GTOS_CONTENT_WINDOW_INTERACTION_TEST)
#include "base/threading/platform_thread.h"
#endif
#include "base/time/time.h"
#include "base/version_info/version_info.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace {

int CheckRuntimeErrors() {
    const auto code = std::error_code(EINVAL, std::system_category());
    errno = EIO;
    const auto message = code.message();
    if (message.empty() || errno != EIO || code != std::errc::invalid_argument ||
        code.default_error_condition() != std::errc::invalid_argument ||
        code == std::errc::io_error)
        return 201;

    const auto future_code = std::make_error_code(std::future_errc::broken_promise);
    const std::future_error future_error(std::future_errc::broken_promise);
    std::logic_error copy("initial");
    copy = future_error;
    if (future_error.code() != future_code ||
        std::string_view(copy.what()) != future_code.message())
        return 202;
    return 0;
}

#if defined(GTOS_CONTENT_WINDOW_INTERACTION_TEST)
constexpr int kMarkerBegin = 48;
constexpr int kMarkerEnd = 96;

bool InWindowMarker(const GtosInputEvent& event) {
    return event.x >= kMarkerBegin && event.x < kMarkerEnd &&
           event.y >= kMarkerBegin && event.y < kMarkerEnd;
}

bool WaitForWindowInput(gtos::process::NativeWindowAdapter& window,
                        unsigned down, unsigned up, unsigned code) {
    const bool pointer = down == GTOS_INPUT_POINTER_DOWN;
    const auto deadline = base::TimeTicks::Now() + base::Seconds(45);
    GtosInputEvent motion{}, pressed{};
    std::array<GtosInputEvent, GTOS_INPUT_READ_LIMIT> events;
    while (base::TimeTicks::Now() < deadline) {
        const int count = window.ReadInput(events.data(), events.size());
        if (count < 0 || static_cast<unsigned>(count) > events.size()) return false;
        for (int i = 0; i < count; ++i) {
            const auto& event = events[i];
            if (event.type == GTOS_INPUT_CLOSE || event.type == GTOS_INPUT_RESET) return false;
            if (event.type == GTOS_INPUT_POINTER_MOVE) motion = event;
            if (event.type == down && event.code == code) {
                if (pointer && (!InWindowMarker(event) || motion.type != GTOS_INPUT_POINTER_MOVE ||
                                motion.x != event.x || motion.y != event.y))
                    return false;
                pressed = event;
            }
            if (event.type == up && event.code == code) {
                return pressed.type == down &&
                       (!pointer || (pressed.x == event.x && pressed.y == event.y));
            }
        }
        base::PlatformThread::Sleep(base::Milliseconds(10));
    }
    return false;
}

int CheckWindowInteraction(gtos::process::NativeWindowAdapter& window,
                           std::array<unsigned char, GTOS_WINDOW_LARGE_BYTES>& frame) {
    const unsigned handle = window.kernel_handle();
    for (unsigned offset = 0; offset < frame.size(); offset += 4) {
        frame[offset] = frame[offset + 1] = frame[offset + 2] = 0x20;
        frame[offset + 3] = 255;
    }
    auto present = [&](unsigned char red, unsigned char green, unsigned char blue) {
        for (int y = kMarkerBegin; y < kMarkerEnd; ++y) {
            for (int x = kMarkerBegin; x < kMarkerEnd; ++x) {
                const unsigned offset = (y * GTOS_WINDOW_LARGE_WIDTH + x) * 4;
                frame[offset] = red;
                frame[offset + 1] = green;
                frame[offset + 2] = blue;
            }
        }
        return gtos::process::PresentLargeNativeWindowFrame(handle, frame.data(), frame.size()) == 0;
    };
    if (!present(0xA5, 0x2B, 0xC7) ||
        !WaitForWindowInput(window, GTOS_INPUT_POINTER_DOWN, GTOS_INPUT_POINTER_UP, 1))
        return 220;
    if (!present(0x12, 0xA8, 0x6D) ||
        !WaitForWindowInput(window, GTOS_INPUT_KEY_DOWN, GTOS_INPUT_KEY_UP, 'g'))
        return 221;
    if (!present(0xF4, 0xAF, 0x22) ||
        !WaitForWindowInput(window, GTOS_INPUT_KEY_DOWN, GTOS_INPUT_KEY_UP, 'q'))
        return 222;
    return 0;
}
#endif

int CheckBrowserWindow() {
    using namespace gtos::process;
    constexpr unsigned kWidth = GTOS_WINDOW_LARGE_WIDTH;
    constexpr unsigned kHeight = GTOS_WINDOW_LARGE_HEIGHT;
    static std::array<unsigned char, GTOS_WINDOW_LARGE_BYTES> frame{};
    NativeWindowAdapter window;
    if (window.Create(kWidth, kHeight) != GTOS_WINDOW_ERR_RANGE ||
        window.CreateLarge(kWidth, kHeight) <= 0)
        return 204;
    for (unsigned offset = 0; offset < frame.size(); offset += 4) {
        frame[offset] = 0x31;
        frame[offset + 1] = 0x43;
        frame[offset + 2] = 0x59;
        frame[offset + 3] = 255;
    }
    const unsigned handle = window.kernel_handle();
    GtosWindowInfoResponse info{};
    if (PresentLargeNativeWindowFrame(handle, frame.data(), frame.size() - 4) !=
            GTOS_WINDOW_ERR_BAD_SIZE ||
        PresentLargeNativeWindowFrame(handle, frame.data(), frame.size()) != 0 ||
        window.Info(&info) != 0 || info.frame_generation != 1 ||
        info.width != kWidth || info.height != kHeight || info.total != frame.size())
        return 205;
    frame.back() = 0;
    if (PresentLargeNativeWindowFrame(handle, frame.data(), frame.size()) !=
            GTOS_WINDOW_ERR_BAD_PIXELS ||
        window.Info(&info) != 0 || info.frame_generation != 1)
        return 206;
#if defined(GTOS_CONTENT_WINDOW_INTERACTION_TEST)
    if (const int status = CheckWindowInteraction(window, frame)) return status;
#endif
    if (window.Close() != 0 ||
        PresentLargeNativeWindowFrame(handle, frame.data(), frame.size()) !=
            GTOS_WINDOW_ERR_BAD_HANDLE)
        return 206;
    return 0;
}

int CheckSystemInfoAndTime() {
    const auto info = gtos::native::CurrentProcessInfo();
    base::SystemMemoryInfo memory;
    if (!base::GetSystemMemoryInfo(&memory) || memory.total == base::ByteSize() ||
        memory.free > memory.total ||
        base::SysInfo::NumberOfProcessors() != static_cast<int>(info.scheduler_cpu_count) ||
        base::SysInfo::AmountOfVirtualMemory() !=
            base::ByteSize(static_cast<uint64_t>(info.maximum_pages) * info.page_bytes) ||
        base::SysInfo::OperatingSystemName() != "GTOS" ||
        base::SysInfo::OperatingSystemArchitecture() != "x86")
        return 181;

    int32_t major = -1;
    int32_t minor = -1;
    int32_t patch = -1;
    base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &patch);
    if (version_info::GetOSType() != "GTOS" ||
        base::SysInfo::OperatingSystemVersion() != GTOS_VERSION_STRING ||
        major != GTOS_VERSION_MAJOR || minor != GTOS_VERSION_MINOR ||
        patch != GTOS_VERSION_PATCH)
        return 203;

    const auto before_epoch = base::Time::UnixEpoch() - base::Milliseconds(500);
    const auto leap_day = base::Time::FromMillisecondsSinceUnixEpoch(951782400000);
    base::Time::Exploded calendar;
    before_epoch.UTCExplode(&calendar);
    if (calendar.year != 1969 || calendar.month != 12 || calendar.day_of_month != 31 ||
        calendar.hour != 23 || calendar.minute != 59 || calendar.second != 59 ||
        calendar.millisecond != 500)
        return 182;
    leap_day.UTCExplode(&calendar);
    base::Time restored;
    if (calendar.year != 2000 || calendar.month != 2 || calendar.day_of_month != 29 ||
        !base::Time::FromUTCExploded(calendar, &restored) || restored != leap_day)
        return 183;
    const auto process = base::Process::Current();
    if (!process.IsValid() || !process.is_current() ||
        process.Pid() != static_cast<base::ProcessId>(info.process_id) ||
        base::Process::Open(base::kNullProcessId).IsValid())
        return 194;
    const auto metrics = base::ProcessMetrics::CreateCurrentProcessMetrics();
    const auto first = metrics->GetCumulativeCPUUsage();
    const auto second = metrics->GetCumulativeCPUUsage();
    if (!first.has_value() || !second.has_value() || *first > *second ||
        base::ProcessMetrics::CreateProcessMetrics(base::kNullProcessHandle)
                ->GetCumulativeCPUUsage().has_value())
        return 195;
    if (base::SysInfo::CPUModelName() != base::CPU().cpu_brand() ||
        metrics->GetMemoryInfo().has_value() || base::GetMaxFds() != 64)
        return 217;
    return 0;
}

int CheckFileStreams(const base::FilePath& directory) {
    const auto path = directory.AppendASCII("stream.txt");
    if (!base::WriteFile(path, "ABCD")) return 224;
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WRITE);
    if (!file.IsValid() || file.Seek(base::File::FROM_BEGIN, 1) != 1) return 224;
    const int descriptor = file.GetPlatformFile();
    const int peer = dup(descriptor);
    if (peer <= 0) return 224;
    FILE* output = base::FileToFILE(std::move(file), "wb");
    if (!output || fileno(output) != descriptor || fputc('X', output) != 'X' ||
        fflush(output) != 0 || lseek(peer, 0, SEEK_CUR) != 2 ||
        fclose(output) != 0 || lseek(peer, 0, SEEK_END) != 4)
        return 225;
    errno = 0;
    if (lseek(descriptor, 0, SEEK_CUR) != -1 || errno != EBADF) return 226;
    FILE* append = fdopen(peer, "a+");
    if (!append || fseek(append, 99, SEEK_SET) != 0 ||
        fputc('Y', append) != 'Y' || fflush(append) != 0 ||
        lseek(peer, 0, SEEK_CUR) != 5 || fseek(append, 1, SEEK_SET) != 0 ||
        fgetc(append) != 'X' || fclose(append) != 0)
        return 227;
    base::File reader(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    const int read_handle = reader.GetPlatformFile();
    if (!reader.IsValid()) return 228;
    errno = 0;
    if (fdopen(read_handle, "a") || errno != EBADF ||
        lseek(read_handle, 0, SEEK_CUR) != 0)
        return 228;
    errno = 0;
    if (fdopen(read_handle, "a?+") || errno != EINVAL ||
        lseek(read_handle, 0, SEEK_CUR) != 0)
        return 229;
    errno = 0;
    FILE* denied = base::FileToFILE(std::move(reader), "a");
    if (denied) {
        fclose(denied);
        return 230;
    }
    if (errno != EBADF) return 232;
    if (lseek(read_handle, 0, SEEK_CUR) != -1 || errno != EBADF) return 233;
    std::string contents;
    if (!base::ReadFileToString(path, &contents) || contents != "AXCDY" ||
        !base::DeleteFile(path))
        return 231;
    return 0;
}

int CheckFileServices(const base::FilePath& directory,
                      const base::FilePath& source) {
    const auto target = directory.AppendASCII("copied.txt");
    base::FilePath normalized;
    if (!base::PathIsReadable(directory) || !base::PathIsWritable(source) ||
        !base::PathIsReadable(source) || base::PathIsReadable(target) ||
        base::PathIsWritable(target) || base::IsLink(source) ||
        !base::GetFileDriveInfo(source) || base::GetFileDriveInfo(target) ||
        !base::NormalizeFilePath(directory.AppendASCII("./data.txt"), &normalized) ||
        normalized != source || base::NormalizeFilePath(target, &normalized) ||
        normalized != source)
        return 218;
    if (base::CopyFile(source, source) || base::CopyFile(target, source) ||
        !base::CopyFile(source, target) || !base::ContentsEqual(source, target))
        return 219;
    base::FilePath first, second, rejected;
    if (!base::CreateTemporaryDirInDir(directory, "copy", &first) ||
        !base::CreateTemporaryDirInDir(directory, "copy", &second) ||
        first == second || first.DirName() != directory ||
        second.DirName() != directory || !base::DirectoryExists(first) ||
        !base::DirectoryExists(second) ||
        base::CreateTemporaryDirInDir(directory, "../bad", &rejected) ||
        !rejected.empty() || base::SetCurrentDirectory(directory) ||
        !base::SetCurrentDirectory(base::FilePath("/")))
        return 221;
    const auto numbers = directory.AppendASCII("numbers");
    {
        base::ScopedFILE output(fopen(numbers.value().c_str(), "w"));
        if (!output || fputs("GTOS -42 2a", output.get()) < 0 ||
            fflush(output.get()) != 0)
            return 222;
    }
    base::ScopedFILE input(fopen(numbers.value().c_str(), "r"));
    std::array<char, 5> text{};
    int value = 0;
    unsigned hex = 0;
    if (!input || fscanf(input.get(), "%4s %d %x", text.data(), &value, &hex) != 3 ||
        std::string_view(text.data(), 4) != "GTOS" || value != -42 || hex != 42 ||
        fscanf(input.get(), "%d", &value) != EOF)
        return 223;
    return CheckFileStreams(directory);
}

int CheckDirectoryAndTemporaryFiles() {
    if (fnmatch("*.txt", "data.txt", FNM_NOESCAPE) != 0 ||
        fnmatch("*.txt", "ignore.bin", FNM_NOESCAPE) != FNM_NOMATCH ||
        fnmatch("data?.txt", "data.txt", FNM_NOESCAPE) != FNM_NOMATCH ||
        fnmatch("data[1-3][!0-9].txt", "data2A.txt", FNM_NOESCAPE) != 0 ||
        fnmatch("[[:digit:]]", "q", FNM_NOESCAPE) != FNM_NOMATCH ||
        fnmatch("[[:digit:]]", "7", FNM_NOESCAPE) != 0 ||
        fnmatch("\\*", "*", 0) != 0 ||
        fnmatch("\\*", "*", FNM_NOESCAPE) != FNM_NOMATCH ||
        fnmatch("*.txt", "sub/data.txt", FNM_PATHNAME) != FNM_NOMATCH ||
        fnmatch("*/.profile", "sub/.profile", FNM_PATHNAME | FNM_PERIOD) != 0 ||
        fnmatch("*", ".profile", FNM_PERIOD) != FNM_NOMATCH ||
        fnmatch("a*[c-e]?", "AxyD1", FNM_CASEFOLD) != 0 ||
        fnmatch("[abc", "[abc", FNM_NOESCAPE) != 0)
        return 203;
    const base::FilePath directory(GTOS_CONTENT_ASSET_DIRECTORY "/gtos-base-dir");
    const auto nested = directory.AppendASCII("nested");
    const auto item = nested.AppendASCII("data.txt");
    const auto ignored = nested.AppendASCII("ignore.bin");
    if (!base::CreateDirectory(nested) || !base::DirectoryExists(nested) ||
        !base::WriteFile(item, std::string_view("native directory")) ||
        !base::WriteFile(ignored, std::string_view("ignored")))
        return 196;
    base::FileEnumerator entries(directory, true, base::FileEnumerator::FILES,
                                 "*.txt", base::FileEnumerator::FolderSearchPolicy::ALL);
    if (entries.Next() != item) return 197;
    const auto metadata = entries.GetInfo();
    if (metadata.GetSize() != 16 || !metadata.GetLastModifiedTime().is_null()) return 204;
    if (!entries.Next().empty()) return 205;
    if (const int status = CheckFileServices(nested, item)) return status;

    base::FilePath first, second, stream_path, rejected;
    auto first_file = base::CreateAndOpenTemporaryFileInDir(nested, &first, 0, "prefs");
    auto second_file = base::CreateAndOpenTemporaryFileInDir(nested, &second, 0, "prefs");
    if (!first_file.IsValid() || !second_file.IsValid() || first == second ||
        base::GetNamePrefixForTemporaryFile(first) != "prefs" ||
        base::CreateAndOpenTemporaryFileInDir(nested, &rejected, 0, "../bad").IsValid() ||
        !rejected.empty())
        return 198;
    first_file.Close();
    second_file.Close();
    auto stream = base::CreateAndOpenTemporaryStreamInDir(nested, &stream_path);
    if (!stream || putc('g', stream.get()) != 'g' || fflush(stream.get()) != 0)
        return 199;
    stream.reset();
    std::array<char, 3> bytes{};
    if (base::ReadFile(stream_path, base::span(bytes)) != 1 || bytes.front() != 'g' ||
        !base::DeletePathRecursively(directory) || base::PathExists(directory))
        return 200;
    return 0;
}

int CheckNativeFile() {
    using base::File;
    using base::FilePath;
    const FilePath source(GTOS_CONTENT_ASSET_DIRECTORY "/gtos-base-source");
    const FilePath target(GTOS_CONTENT_ASSET_DIRECTORY "/gtos-base-target");
    const FilePath directory(GTOS_CONTENT_ASSET_DIRECTORY);
    std::array<uint8_t, 8193> payload{};
    for (size_t i = 0; i < payload.size(); ++i)
        payload.at(i) = static_cast<uint8_t>((i * 19 + 7) % 251);
    {
        File file(source, File::FLAG_CREATE_ALWAYS | File::FLAG_READ | File::FLAG_WRITE);
        if (!file.IsValid() || file.WriteAtCurrentPos(base::span(payload)) != payload.size() ||
            !file.Flush() || file.GetLength() != static_cast<int64_t>(payload.size()))
            return 184;
        constexpr int64_t kCursor = 123;
        std::array<uint8_t, 31> chunk{};
        const auto expected = base::span(payload).subspan(4091u, chunk.size());
        if (file.Seek(File::FROM_BEGIN, kCursor) != kCursor ||
            file.Read(4091, base::span(chunk)) != chunk.size() ||
            !std::ranges::equal(chunk, expected) || file.Seek(File::FROM_CURRENT, 0) != kCursor)
            return 185;
        File alias = file.Duplicate();
        if (!alias.IsValid() || alias.Seek(File::FROM_CURRENT, 0) != kCursor ||
            alias.ReadAtCurrentPos(base::span(chunk)) != chunk.size() ||
            file.Seek(File::FROM_CURRENT, 0) != kCursor + chunk.size())
            return 186;
    }
    {
        File old(target, File::FLAG_CREATE_ALWAYS | File::FLAG_WRITE);
        if (!old.IsValid() || old.WriteAtCurrentPos(base::span(payload).first(1u)) != 1)
            return 187;
    }
    if (!base::DirectoryExists(directory) || base::DirectoryExists(source) ||
        !base::ReplaceFile(source, target, nullptr) ||
        base::PathExists(source) || !base::PathExists(target))
        return 188;
    {
        base::MemoryMappedFile view;
        const base::MemoryMappedFile::Region region{4091, 37};
        if (!view.Initialize(File(target, File::FLAG_OPEN | File::FLAG_READ), region) ||
            !std::ranges::equal(view.bytes(), base::span(payload).subspan(4091u, 37u)))
            return 189;
        base::MemoryMappedFile copy;
        if (!copy.Initialize(target, base::MemoryMappedFile::READ_WRITE_COPY))
            return 190;
        copy.mutable_bytes().front() ^= 0xff;
        File stored(target, File::FLAG_OPEN | File::FLAG_READ);
        std::array<uint8_t, 1> first{};
        if (stored.Read(0, base::span(first)) != 1 || first.front() != payload.front())
            return 191;
    }
    base::MemoryMappedFile shared;
    if (shared.Initialize(target, base::MemoryMappedFile::READ_WRITE))
        return 192;
    if (!base::DeleteFile(target) || base::PathExists(target) || !base::DeleteFile(target))
        return 193;
    return 0;
}

}  // namespace

int TestContentBasePlatform() {
    if (const int status = CheckRuntimeErrors()) return status;
    if (const int status = CheckSystemInfoAndTime()) return status;
    if (const int status = CheckBrowserWindow()) return status;
    if (const int status = CheckNativeFile()) return status;
    return CheckDirectoryAndTemporaryFiles();
}
