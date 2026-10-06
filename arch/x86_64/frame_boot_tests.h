#ifndef GTOS_X64_FRAME_BOOT_TESTS_H
#define GTOS_X64_FRAME_BOOT_TESTS_H
#include "frame_pool.h"
/* BSP guest fixtures: synthetic bytes are parser input only. Any successful
 * initialization below is based on the actual resident boot-information copy. */
void frame_boot_tests(const void *private_copy, size_t size, uint64_t original,
                      const struct frame_platform *platform);
#endif
