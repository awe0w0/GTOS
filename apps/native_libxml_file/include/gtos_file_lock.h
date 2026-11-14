#ifndef GTOS_FILE_LOCK_H
#define GTOS_FILE_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

// Opens or creates a read/write file with the native exclusive advisory lock.
// The lock survives dup() and is released by the last close or owner reap.
int gtos_open_locked(const char *path);

#ifdef __cplusplus
}
#endif

#endif
