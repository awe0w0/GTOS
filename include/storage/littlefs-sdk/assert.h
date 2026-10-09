#ifndef GTOS_LFS_ASSERT_H
#define GTOS_LFS_ASSERT_H
#include <storage/littlefs_port.h>
#define assert(test) LFS_ASSERT(test)
#endif
