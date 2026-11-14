#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <process/abi.h>
#include "base/files/file_path.h"
#include "components/services/filesystem/util.h"
#include "components/services/storage/indexed_db/scopes/varint_coding.h"
#include "content/browser/indexed_db/indexed_db_leveldb_coding.h"

namespace {

template <unsigned Size>
void Say(const char (&message)[Size]) {
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
}

bool CheckPath() {
  const base::FilePath directory("/content/private");
  const base::FilePath sentinel("previous");
  base::FilePath path = sentinel;
  using Error = base::File::Error;
  if (filesystem::ValidatePath("data/\xe4\xb8\xad", directory, &path) !=
          Error::FILE_OK ||
      path.value() != "/content/private/data/\xe4\xb8\xad") return false;
  path = sentinel;
  return filesystem::ValidatePath("/root", directory, &path) ==
             Error::FILE_ERROR_ACCESS_DENIED &&
         filesystem::ValidatePath("data/../root", directory, &path) ==
             Error::FILE_ERROR_ACCESS_DENIED &&
         filesystem::ValidatePath(std::string("a\0b", 3), directory, &path) ==
             Error::FILE_ERROR_INVALID_OPERATION &&
         filesystem::ValidatePath("\xc0\xaf", directory, &path) ==
             Error::FILE_ERROR_INVALID_OPERATION &&
         path == sentinel;
}

bool CheckInts() {
  using content::indexed_db::DecodeInt;
  using content::indexed_db::DecodeVarInt;
  using content::indexed_db::EncodeInt;
  using content::indexed_db::EncodeVarInt;
  for (int64_t expected : {int64_t{0}, int64_t{127}, int64_t{128}, int64_t{300},
                          std::numeric_limits<int64_t>::max()}) {
    std::string encoded;
    EncodeVarInt(expected, &encoded);
    encoded.push_back('z');
    std::string_view value(encoded);
    int64_t number = -1;
    if (!DecodeVarInt(&value, &number) || value != "z" || number != expected)
      return false;
    encoded.clear();
    EncodeInt(expected, &encoded);
    value = encoded;
    if (!DecodeInt(&value, &number) || !value.empty() || number != expected)
      return false;
  }

  for (std::string_view invalid :
       {std::string_view{}, std::string_view("\x80", 1),
        std::string_view("\x80\0", 2), std::string_view("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x02", 10)}) {
    std::string_view value = invalid;
    int64_t number = 351;
    if (DecodeVarInt(&value, &number) || value != invalid || number != 351)
      return false;
  }
  int64_t number = 321;
  std::string_view oversized("abcdefghi");
  if (DecodeInt(&oversized, &number) || oversized != "abcdefghi" || number != 321)
    return false;
  std::string_view signed_value("\xff\xff\xff\xff\xff\xff\xff\xff", 8);
  return DecodeInt(&signed_value, &number) && signed_value.empty() && number == -1;
}

}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
  if (!CheckPath()) return 201;
  if (!CheckInts()) return 202;
  Say("GTOS CONTENT STORAGE BOUNDS PASS\n");
  return 37;
}
