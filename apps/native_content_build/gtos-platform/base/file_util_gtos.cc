// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <process/file_abi.h>
#include <unistd.h>

#include "base/files/drive_info.h"
#include "base/files/file_enumerator.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/scoped_blocking_call.h"
#include "build/branding_buildflags.h"

namespace base {
namespace {

constexpr size_t kTemporarySuffixLength = 6;

bool ValidTemporaryPrefix(FilePath::StringViewType prefix) {
  return prefix.find('/') == prefix.npos && prefix.find('\0') == prefix.npos;
}

FilePath TemporaryName(const FilePath& directory,
                       FilePath::StringViewType prefix) {
  constexpr std::string_view alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string suffix(kTemporarySuffixLength, '0');
  for (char& digit : suffix) {
    digit = alphabet[static_cast<size_t>(RandGenerator(alphabet.size()))];
  }
  return directory.Append(FormatTemporaryFileName(
      prefix.empty() ? suffix : StrCat({prefix, ".", suffix}), true));
}

}  // namespace

bool DeleteFile(const FilePath& path) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (path.empty()) {
    return true;
  }
  const char* name = path.value().c_str();
  struct stat info {};
  if (stat(name, &info) != 0) {
    return errno == ENOENT;
  }
  const int result = S_ISDIR(info.st_mode) ? rmdir(name) : unlink(name);
  return result == 0 || errno == ENOENT;
}

bool ReplaceFile(const FilePath& from_path,
                 const FilePath& to_path,
                 File::Error* error) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (rename(from_path.value().c_str(), to_path.value().c_str()) == 0) {
    return true;
  }
  if (error) {
    *error = File::GetLastFileError();
  }
  return false;
}

bool PathExists(const FilePath& path) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  struct stat info {};
  return stat(path.value().c_str(), &info) == 0;
}

bool DirectoryExists(const FilePath& path) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  struct stat info {};
  return stat(path.value().c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool PathIsReadable(const FilePath& path) {
  return DirectoryExists(path) ||
         File(path, File::FLAG_OPEN | File::FLAG_READ).IsValid();
}

bool PathIsWritable(const FilePath& path) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  // Opening a file without truncation checks the store's write policy.
  // Directories have no equivalent permission query.
  const int fd = open(path.value().c_str(), O_WRONLY);
  return fd >= 0 && close(fd) == 0;
}

bool IsLink(const FilePath&) {
  return false;
}

std::optional<DriveInfo> GetFileDriveInfo(const FilePath& path) {
  if (!PathExists(path)) {
    return std::nullopt;
  }
  return DriveInfo();
}

int GetMaximumPathComponentLength(const FilePath& path) {
  if (!DirectoryExists(path)) {
    return -1;
  }
  return sizeof(GtosFileInfo::name) - 1;
}

bool GetCurrentDirectory(FilePath* path) {
  // Native paths are relative to the mounted volume root. There is no chdir.
  const FilePath root("/");
  if (!DirectoryExists(root)) {
    return false;
  }
  *path = root;
  return true;
}

FilePath MakeAbsoluteFilePath(const FilePath& input) {
  if (input.empty() || input.value().size() >= GTOS_FILE_PATH_LIMIT) {
    return {};
  }

  // The native store has no symbolic links. Resolve dot components while
  // checking each directory, including components later removed by "..".
  const FilePath root("/");
  FilePath path = root;
  const auto components = SplitStringPiece(
      input.value(), "/", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY);
  for (size_t i = 0; i < components.size(); ++i) {
    const auto component = components[i];
    if (component == ".") {
      continue;
    }
    if (component == "..") {
      if (path == root) {
        return {};
      }
      path = path.DirName();
      continue;
    }
    path = path.Append(component);
    if (i + 1 < components.size() && !DirectoryExists(path)) {
      return {};
    }
  }
  const bool exists =
      input.EndsWithSeparator() ? DirectoryExists(path) : PathExists(path);
  return exists ? path : FilePath();
}

bool NormalizeFilePath(const FilePath& path, FilePath* real_path) {
  FilePath normalized = MakeAbsoluteFilePath(path);
  if (normalized.empty()) {
    return false;
  }
  *real_path = std::move(normalized);
  return true;
}

bool SetCurrentDirectory(const FilePath& path) {
  return MakeAbsoluteFilePath(path) == FilePath("/");
}

bool CopyFile(const FilePath& from_path, const FilePath& to_path) {
  if (from_path.ReferencesParent() || to_path.ReferencesParent()) {
    return false;
  }
  const FilePath source = MakeAbsoluteFilePath(from_path);
  if (source.empty() || source == MakeAbsoluteFilePath(to_path)) {
    return false;
  }
  File input(source, File::FLAG_OPEN | File::FLAG_READ);
  if (!input.IsValid()) {
    return false;
  }
  File output(to_path, File::FLAG_CREATE_ALWAYS | File::FLAG_WRITE);
  return output.IsValid() && CopyFileContents(input, output) && output.Flush();
}

bool PreReadFile(const FilePath&, bool, bool, int64_t) {
  // Reading into a temporary buffer does not populate a file-page cache.
  return false;
}

FilePath GetHomeDir() {
  if (const char* home = getenv("HOME"); home && *home) {
    const FilePath configured(home);
    if (configured.IsAbsolute() && !configured.ReferencesParent() &&
        DirectoryExists(configured)) {
      return configured;
    }
  }
  FilePath fallback;
  return GetTempDir(&fallback) ? fallback : FilePath("/");
}

bool DeletePathRecursively(const FilePath& path) {
  if (path.empty()) {
    return true;
  }
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  struct stat info {};
  if (stat(path.value().c_str(), &info) != 0) {
    return errno == ENOENT;
  }
  if (!S_ISDIR(info.st_mode)) {
    return DeleteFile(path);
  }
  FileEnumerator children(path, false,
                          FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                          {}, FileEnumerator::FolderSearchPolicy::ALL,
                          FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (auto child = children.Next(); !child.empty(); child = children.Next()) {
    if (!DeletePathRecursively(child)) {
      return false;
    }
  }
  return children.GetError() == File::FILE_OK && DeleteFile(path);
}

bool CreateDirectoryAndGetError(const FilePath& path, File::Error* error) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (path.empty() || path.ReferencesParent()) {
    if (error) {
      *error = File::FILE_ERROR_INVALID_OPERATION;
    }
    return false;
  }
  if (DirectoryExists(path)) {
    return true;
  }
  const FilePath parent = path.DirName();
  if (parent != path && !CreateDirectoryAndGetError(parent, error)) {
    return false;
  }
  if (mkdir(path.value().c_str(), 0700) == 0) {
    return true;
  }
  const int failure = errno;
  if (failure == EEXIST && DirectoryExists(path)) {
    return true;
  }
  if (error) {
    *error = File::OSErrorToFileError(failure);
  }
  return false;
}

bool GetFileInfo(const FilePath& path, File::Info* info) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  struct stat value {};
  if (stat(path.value().c_str(), &value) != 0) {
    return false;
  }
  File::Info result;
  result.size = value.st_size;
  result.is_directory = S_ISDIR(value.st_mode);
  *info = result;
  return true;
}

FILE* OpenFile(const FilePath& path, cstring_view mode) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  return fopen(path.value().c_str(), mode.c_str());
}

FILE* FileToFILE(File file, const char* mode) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  FILE* stream = fdopen(file.GetPlatformFile(), mode);
  if (stream) {
    file.TakePlatformFile();
  } else {
    const int error = errno;
    file.Close();
    errno = error;
  }
  return stream;
}

std::optional<uint64_t> ReadFile(const FilePath& path, span<char> buffer) {
  File file(path, File::FLAG_OPEN | File::FLAG_READ);
  return file.ReadAtCurrentPos(as_writable_byte_span(buffer));
}

bool WriteFile(const FilePath& path, span<const uint8_t> data) {
  File file(path, File::FLAG_CREATE_ALWAYS | File::FLAG_WRITE);
  return file.IsValid() && file.WriteAtCurrentPosAndCheck(data) && file.Flush();
}

bool GetTempDir(FilePath* path) {
  const char* configured = getenv("TMPDIR");
  if (!configured || !*configured) {
    configured = getenv("GTOS_CONTENT_ASSET_DIR");
  }
  const FilePath candidate(configured && *configured ? configured : "/tmp");
  if (!candidate.IsAbsolute() || candidate.ReferencesParent() ||
      !DirectoryExists(candidate)) {
    return false;
  }
  *path = candidate;
  return true;
}

bool CreateTemporaryDirInDir(const FilePath& directory,
                            FilePath::StringViewType prefix,
                            FilePath* path) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (!ValidTemporaryPrefix(prefix) || !DirectoryExists(directory)) {
    return false;
  }
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    FilePath candidate = TemporaryName(directory, prefix);
    if (mkdir(candidate.value().c_str(), 0700) == 0) {
      *path = std::move(candidate);
      return true;
    }
    if (errno != EEXIST) {
      return false;
    }
  }
  return false;
}

bool CreateNewTempDirectory(FilePath::StringViewType prefix, FilePath* path) {
  FilePath directory;
  return GetTempDir(&directory) &&
         CreateTemporaryDirInDir(directory, prefix, path);
}

FilePath FormatTemporaryFileName(FilePath::StringViewType identifier,
                                 bool hidden) {
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  constexpr std::string_view prefix = "com.google.Chrome";
#else
  constexpr std::string_view prefix = "org.chromium.Chromium";
#endif
  return FilePath(StrCat({hidden ? "." : "", prefix, ".", identifier}));
}

std::optional<FilePath::StringType> GetNamePrefixForTemporaryFile(
    const FilePath& path) {
  const auto name = path.BaseName().value();
  const auto platform_prefix = FormatTemporaryFileName({}, true).value();
  if (!StartsWith(name, platform_prefix) ||
      name.size() <= platform_prefix.size() + kTemporarySuffixLength + 1) {
    return std::nullopt;
  }
  const size_t separator = name.size() - kTemporarySuffixLength - 1;
  if (name[separator] != '.' ||
      name.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz",
                             separator + 1) != name.npos) {
    return std::nullopt;
  }
  return name.substr(platform_prefix.size(), separator - platform_prefix.size());
}

File CreateAndOpenTemporaryFileInDir(const FilePath& directory,
                                     FilePath* path,
                                     uint32_t,
                                     FilePath::StringViewType prefix) {
  if (!ValidTemporaryPrefix(prefix)) {
    return File(File::FILE_ERROR_INVALID_OPERATION);
  }
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    FilePath candidate = TemporaryName(directory, prefix);
    File file(candidate, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
    if (file.IsValid()) {
      *path = std::move(candidate);
      return file;
    }
    if (file.error_details() != File::FILE_ERROR_EXISTS) {
      return file;
    }
  }
  return File(File::FILE_ERROR_EXISTS);
}

bool CreateTemporaryFileInDir(const FilePath& directory, FilePath* path) {
  return CreateAndOpenTemporaryFileInDir(directory, path).IsValid();
}

ScopedFILE CreateAndOpenTemporaryStreamInDir(const FilePath& directory,
                                             FilePath* path) {
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    FilePath candidate = TemporaryName(directory, {});
    if (ScopedFILE stream(OpenFile(candidate, "w+x")); stream) {
      *path = std::move(candidate);
      return stream;
    }
    if (errno != EEXIST) {
      return nullptr;
    }
  }
  return nullptr;
}

namespace internal {

bool MoveUnsafe(const FilePath& from_path, const FilePath& to_path) {
  return ReplaceFile(from_path, to_path, nullptr);
}

}  // namespace internal

}  // namespace base
