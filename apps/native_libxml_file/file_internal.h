#ifndef GTOS_POSIX_FILE_INTERNAL_H
#define GTOS_POSIX_FILE_INTERNAL_H

namespace gtos_file {
int call(unsigned operation, const void *request, unsigned bytes);
int translate(int result);
int path_bytes(const char *path, unsigned *bytes);
}

#endif
