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

#include <fnmatch.h>
#include <stdio.h>
#include <storage/content_volume_layout.h>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/process/process.h"
#include "base/process/process_metrics.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
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
    return 0;
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
    if (const int status = CheckNativeFile()) return status;
    return CheckDirectoryAndTemporaryFiles();
}
