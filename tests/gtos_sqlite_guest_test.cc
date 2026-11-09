// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <clocale>
#include <memory>
#include <string_view>
#include <thread>

#include <storage/content_volume_layout.h>

#include "third_party/sqlite/sqlite3.h"

namespace {

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

bool Execute(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

Statement Prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
    return Statement(statement, sqlite3_finalize);
}

bool Integer(sqlite3* db, const char* sql, int expected) {
    const auto query = Prepare(db, sql);
    return query && sqlite3_step(query.get()) == SQLITE_ROW &&
           sqlite3_column_int(query.get(), 0) == expected &&
           sqlite3_step(query.get()) == SQLITE_DONE;
}

bool Text(sqlite3* db, const char* sql, std::string_view expected) {
    const auto query = Prepare(db, sql);
    if (!query || sqlite3_step(query.get()) != SQLITE_ROW) return false;
    const auto* value = sqlite3_column_text(query.get(), 0);
    return value && std::string_view(reinterpret_cast<const char*>(value)) == expected &&
           sqlite3_step(query.get()) == SQLITE_DONE;
}

}  // namespace

int TestSqliteVfs() {
    if (sqlite3_initialize() != SQLITE_OK || sqlite3_threadsafe() == 0) return 47;
    auto* vfs = sqlite3_vfs_find(nullptr);
    if (!vfs || std::string_view(vfs->zName) != "gtos" || vfs->iVersion != 3) return 48;

    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-sqlite-probe.db";
    constexpr char alias[] = GTOS_CONTENT_ASSET_DIRECTORY "//./gtos-sqlite-probe.db";
    std::array<char, 64> normalized{};
    if (vfs->xFullPathname(vfs, alias, normalized.size(), normalized.data()) != SQLITE_OK ||
        std::string_view(normalized.data()) != path ||
        vfs->xFullPathname(vfs, alias, 2, normalized.data()) != SQLITE_CANTOPEN ||
        std::string_view(normalized.data()) != path)
        return 49;
    sqlite3_int64 now = 0;
    if (vfs->xCurrentTimeInt64(vfs, &now) != SQLITE_OK || now < 211813444800000LL)
        return 50;
    const char* locale = std::setlocale(LC_CTYPE, nullptr);
    if (!locale || std::string_view(locale) != "C" ||
        !std::setlocale(LC_CTYPE, "POSIX") || std::setlocale(LC_CTYPE, "invalid"))
        return 60;

    sqlite3* handle = nullptr;
    const int opened = sqlite3_open_v2(path, &handle, SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    Database db(handle, sqlite3_close);
    if (opened != SQLITE_OK) return 51;
    sqlite3_mutex* mutex = sqlite3_db_mutex(db.get());
    if (!mutex) return 61;
    sqlite3_mutex_enter(mutex);
    sqlite3_mutex_enter(mutex);
    sqlite3_mutex_leave(mutex);
    int contention = SQLITE_ERROR;
    std::thread contender([&] {
        contention = sqlite3_mutex_try(mutex);
        if (contention == SQLITE_OK) sqlite3_mutex_leave(mutex);
    });
    contender.join();
    sqlite3_mutex_leave(mutex);
    if (contention != SQLITE_BUSY) return 62;
    if (!Execute(db.get(), "PRAGMA synchronous=FULL; CREATE TABLE sample(n INTEGER);"
                            "INSERT INTO sample VALUES(11),(31);") ||
        !Integer(db.get(), "SELECT sum(n) FROM sample", 42))
        return 52;

    handle = nullptr;
    const int conflict = sqlite3_open_v2(alias, &handle, SQLITE_OPEN_READONLY, nullptr);
    Database competing(handle, sqlite3_close);
    if (conflict != SQLITE_BUSY) return 53;
    competing.reset();
    if (!Execute(db.get(), "BEGIN; INSERT INTO sample VALUES(100); ROLLBACK;") ||
        !Integer(db.get(), "SELECT sum(n) FROM sample", 42))
        return 54;

    if (!Text(db.get(), "PRAGMA locking_mode=EXCLUSIVE", "exclusive") ||
        !Text(db.get(), "PRAGMA journal_mode=WAL", "wal") ||
        !Execute(db.get(), "INSERT INTO sample VALUES(8);") ||
        !Integer(db.get(), "SELECT sum(n) FROM sample", 50) ||
        sqlite3_close(db.release()) != SQLITE_OK)
        return 55;

    handle = nullptr;
    const int reopened = sqlite3_open_v2(path, &handle, SQLITE_OPEN_READWRITE, nullptr);
    db.reset(handle);
    if (reopened != SQLITE_OK ||
        !Text(db.get(), "PRAGMA locking_mode=EXCLUSIVE", "exclusive") ||
        !Integer(db.get(), "SELECT sum(n) FROM sample", 50) ||
        sqlite3_close(db.release()) != SQLITE_OK)
        return 56;
    handle = nullptr;
    const int missing = sqlite3_open_v2(GTOS_CONTENT_ASSET_DIRECTORY "/gtos-sqlite-absent.db",
                                       &handle, SQLITE_OPEN_READONLY, nullptr);
    db.reset(handle);
    if (missing != SQLITE_CANTOPEN) return 57;
    if (vfs->xDelete(vfs, path, 1) != SQLITE_OK) return 58;
    int exists = 1;
    if (vfs->xAccess(vfs, path, SQLITE_ACCESS_EXISTS, &exists) != SQLITE_OK || exists)
        return 59;
    return 0;
}
