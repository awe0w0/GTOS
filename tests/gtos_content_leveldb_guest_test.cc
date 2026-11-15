// Exercises Chromium's LevelDB file locking on the native GTOS volume.
#include <cerrno>
#include <memory>
#include <string>

#include <gtos_file_lock.h>
#include <process/abi.h>
#include <unistd.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "components/services/storage/public/cpp/filesystem/filesystem_proxy.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

namespace {

using base::File;
using base::FilePath;
using storage::FilesystemProxy;

int CheckLockLifetime() {
  const FilePath directory("/gtos-chromium-locks");
  const FilePath path = directory.AppendASCII("LOCK");
  FilesystemProxy proxy(FilesystemProxy::UNRESTRICTED, directory);
  if (proxy.CreateDirectory(directory) != File::FILE_OK) {
    return 101;
  }

  auto first = proxy.LockFile(path);
  if (!first.has_value()) {
    return 102;
  }
  bool same_process = false;
  auto duplicate = proxy.LockFile(path, &same_process);
  if (duplicate.has_value() || duplicate.error() != File::FILE_ERROR_IN_USE ||
      !same_process || gtos_open_locked(path.value().c_str()) != -1 ||
      errno != EBUSY) {
    return 103;
  }
  if (first.value()->Release() != File::FILE_OK ||
      first.value()->Release() != File::FILE_ERROR_INVALID_OPERATION) {
    return 104;
  }

  const int handle = gtos_open_locked(path.value().c_str());
  if (handle <= 0) {
    return 105;
  }
  same_process = false;
  auto conflict = proxy.LockFile(path, &same_process);
  if (conflict.has_value() || conflict.error() != File::FILE_ERROR_IN_USE ||
      same_process || close(handle) != 0) {
    return 106;
  }
  {
    auto retried = proxy.LockFile(path);
    if (!retried.has_value()) {
      return 107;
    }
  }
  auto reopened = proxy.LockFile(path);
  if (!reopened.has_value() || reopened.value()->Release() != File::FILE_OK) {
    return 108;
  }
  auto traversal = proxy.LockFile(FilePath("/gtos-chromium-locks/../other"));
  if (traversal.has_value() || traversal.error() != File::FILE_ERROR_ACCESS_DENIED) {
    return 109;
  }
  if (!proxy.DeleteFile(path) || !proxy.DeleteFile(directory)) {
    return 110;
  }
  return 0;
}

int CheckLevelDB() {
  constexpr char kPath[] = "/gtos-native-leveldb";
  const std::string key("embedded\0key", 12);
  const std::string value("data\0bytes", 10);
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* storage = nullptr;
  if (!leveldb::DB::Open(options, kPath, &storage).ok()) {
    return 111;
  }
  std::unique_ptr<leveldb::DB> database(storage);
  leveldb::WriteOptions write;
  write.sync = true;
  if (!database->Put(write, key, value).ok()) {
    return 112;
  }
  leveldb::DB* contender = nullptr;
  const auto busy = leveldb::DB::Open(options, kPath, &contender);
  std::unique_ptr<leveldb::DB> unexpected(contender);
  if (!busy.IsIOError() || contender) {
    return 113;
  }
  database.reset();
  storage = nullptr;
  options.create_if_missing = false;
  if (!leveldb::DB::Open(options, kPath, &storage).ok()) {
    return 114;
  }
  database.reset(storage);
  std::string result;
  if (!database->Get(leveldb::ReadOptions(), key, &result).ok() || result != value) {
    return 115;
  }
  database.reset();
  return leveldb::DestroyDB(kPath, options).ok() ? 0 : 116;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  if (const int status = CheckLockLifetime()) {
    return status;
  }
  if (const int status = CheckLevelDB()) {
    return status;
  }
  constexpr char message[] = "GTOS CONTENT LEVELDB LOCK PASS\n";
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(sizeof(message) - 1)
               : "cc", "memory");
  return operation == sizeof(message) - 1 ? 37 : 117;
}
