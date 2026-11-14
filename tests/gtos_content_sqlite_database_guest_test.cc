// Exercises the Chromium SQL wrapper and GTOS VFS in a writable guest volume.
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>

#include <process/abi.h>
#include <sys/stat.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/vfs_wrapper.h"
#include "third_party/sqlite/sqlite3.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bool CheckDatabase() {
  constexpr char filename[] = "/gtos-sql-\xe4\xb8\xad.db";
  const base::FilePath path(filename);
  struct stat info{};
  if (stat(filename, &info) == 0 || errno != ENOENT) {
    return false;
  }
  sql::Database db("Test");
  if (!db.Open(path) ||
      !db.Execute("CREATE TABLE records (id INTEGER PRIMARY KEY, data BLOB)") ||
      !db.Execute("INSERT INTO records VALUES (11, x'001280ff')")) {
    return false;
  }
  db.Close();
  if (stat(filename, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0 || !db.Open(path)) {
    return false;
  }
  {
    constexpr std::array<uint8_t, 4> expected = {0, 0x12, 0x80, 0xff};
    sql::Statement row(db.GetUniqueStatement("SELECT id, data FROM records"));
    if (!row.Step() || row.ColumnInt(0) != 11 ||
        row.ColumnBlob(1) != base::span(expected) ||
        row.Step() || !row.Succeeded()) {
      return false;
    }
  }

  if (db.Execute("INSERT INTO records VALUES (11, x'ff')")) {
    return false;
  }
  const int error = db.GetErrorCode();
  if ((error & 0xff) != SQLITE_CONSTRAINT) {
    return false;
  }
  sql::DatabaseDiagnostics diagnostics;
  const std::string log = db.GetDiagnosticInfo(error, nullptr, &diagnostics);
  if (diagnostics.reported_sqlite_error_code != error ||
      diagnostics.error_code != error || diagnostics.last_errno != 0 ||
      !diagnostics.has_valid_header ||
      !diagnostics.has_valid_schema ||
      !log.contains("errno: " + std::to_string(diagnostics.last_errno))) {
    return false;
  }
  return db.CloseAndDelete() && stat(filename, &info) != 0 && errno == ENOENT;
}

bool CheckVfsFileErrors() {
  sqlite3_vfs* vfs = sqlite3_vfs_find(nullptr);
  if (!vfs || std::string_view(vfs->zName) != sql::kVfsWrapperName) {
    Say("GTOS SQLITE VFS UNAVAILABLE\n");
    return false;
  }
  auto* file = static_cast<sqlite3_file*>(sqlite3_malloc(vfs->szOsFile));
  if (!file) {
    return false;
  }
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets)) {
    sqlite3_free(file);
    return false;
  }
  const auto snapshot = assets.AppendASCII("snapshot_blob.bin");
  int flags = 0;
  const int opened = vfs->xOpen(vfs, snapshot.value().c_str(), file,
                               SQLITE_OPEN_MAIN_DB | SQLITE_OPEN_READONLY,
                               &flags);
  if (opened != SQLITE_OK) {
    std::printf("GTOS SQLITE VFS OPEN code=%d path=%s\n",
                opened, snapshot.value().c_str());
    sqlite3_free(file);
    return false;
  }
  int before = -1;
  const int first = file->pMethods->xFileControl(
      file, SQLITE_FCNTL_LAST_ERRNO, &before);
  constexpr uint8_t byte = 0;
  const int written = file->pMethods->xWrite(file, &byte, sizeof(byte), 0);
  int found = -1;
  const int failed = file->pMethods->xFileControl(
      file, SQLITE_FCNTL_LAST_ERRNO, &found);
  uint8_t read_byte = 0;
  const int read_status = file->pMethods->xRead(file, &read_byte, sizeof(byte), 0);
  int after = -1;
  const int last = file->pMethods->xFileControl(
      file, SQLITE_FCNTL_LAST_ERRNO, &after);
  const int closed = file->pMethods->xClose(file);
  sqlite3_free(file);
  std::printf("GTOS SQLITE VFS CONTROL flags=%d first=%d before=%d "
              "write=%d failed=%d errno=%d read=%d byte=%u "
              "last=%d after=%d close=%d\n",
              flags, first, before, written, failed, found, read_status,
              static_cast<unsigned>(read_byte), last, after, closed);
  return (flags & SQLITE_OPEN_READONLY) && first == SQLITE_OK && before == 0 &&
         written == SQLITE_IOERR_WRITE && failed == SQLITE_OK && found == EBADF &&
         read_status == SQLITE_OK && read_byte == 1 && last == SQLITE_OK &&
         after == EBADF && closed == SQLITE_OK;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  if (!CheckDatabase() ||
      !Say("GTOS SQLITE UTF8 DATABASE LIFECYCLE AND DIAGNOSTICS PASS\n")) {
    return 201;
  }
  if (!CheckVfsFileErrors() || !Say("GTOS SQLITE VFS NATIVE FILE ERROR PASS\n")) {
    return 202;
  }
  return 37;
}
