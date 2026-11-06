#include <process/file_abi.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file_internal.h"

struct GtosDirectoryStream {
  unsigned handle;
  struct dirent entry;
};

extern "C" DIR *opendir(const char *path) {
  unsigned bytes = 0;
  if (gtos_file::path_bytes(path, &bytes) != 0) return 0;
  DIR *directory = static_cast<DIR *>(malloc(sizeof(DIR)));
  if (directory == 0) {
    errno = ENOMEM;
    return 0;
  }
  const GtosFilePathRequest request{GTOS_FILE_ABI_VERSION, reinterpret_cast<unsigned>(path), bytes};
  const int handle = gtos_file::call(GTOS_SYS_FILE_DIR_OPEN, &request, sizeof(request));
  if (handle <= 0) {
    free(directory);
    if (handle < 0) gtos_file::translate(handle);
    else errno = EIO;
    return 0;
  }
  directory->handle = static_cast<unsigned>(handle);
  memset(&directory->entry, 0, sizeof(directory->entry));
  return directory;
}

extern "C" struct dirent *readdir(DIR *directory) {
  if (directory == 0) {
    errno = EBADF;
    return 0;
  }
  GtosFileInfo info{};
  const GtosFileInfoRequest request{GTOS_FILE_ABI_VERSION, directory->handle,
                                  reinterpret_cast<unsigned>(&info), sizeof(info)};
  const int result = gtos_file::call(GTOS_SYS_FILE_DIR_READ, &request, sizeof(request));
  if (result == 0) return 0; // EOF preserves errno and the prior returned entry.
  if (result < 0) {
    gtos_file::translate(result);
    return 0;
  }
  unsigned length = 0;
  while (length < sizeof(info.name) && info.name[length] != 0) ++length;
  if (result != 1 || info.version != GTOS_FILE_ABI_VERSION || length == 0 || length == sizeof(info.name)
      || (info.type != GTOS_FILE_TYPE_REGULAR && info.type != GTOS_FILE_TYPE_DIRECTORY)) {
    errno = EIO;
    return 0;
  }
  struct dirent entry{};
  entry.d_reclen = sizeof(entry);
  entry.d_type = info.type == GTOS_FILE_TYPE_DIRECTORY ? DT_DIR : DT_REG;
  memcpy(entry.d_name, info.name, length + 1);
  directory->entry = entry;
  return &directory->entry;
}

extern "C" void rewinddir(DIR *directory) {
  if (directory == 0) {
    errno = EBADF;
    return;
  }
  const GtosFileControlRequest request{GTOS_FILE_ABI_VERSION, directory->handle};
  const int result = gtos_file::call(GTOS_SYS_FILE_DIR_REWIND, &request, sizeof(request));
  if (result < 0) gtos_file::translate(result);
  else if (result != 0) errno = EIO;
}

extern "C" int dirfd(DIR *directory) {
  if (directory == 0) {
    errno = EBADF;
    return -1;
  }
  return static_cast<int>(directory->handle);
}

extern "C" int closedir(DIR *directory) {
  if (directory == 0) {
    errno = EBADF;
    return -1;
  }
  // The native ABI invalidates a closed handle even when disk close fails.
  // Release the stream buffer exactly once; a retry would reuse an invalid fd.
  const int result = close(static_cast<int>(directory->handle));
  const int saved = errno;
  free(directory);
  errno = saved;
  return result;
}
