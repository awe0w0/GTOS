//===-- POSIX header <sys/types.h> --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LLVM_LIBC_SYS_TYPES_H
#define _LLVM_LIBC_SYS_TYPES_H

#include "../__llvm-libc-common.h"

#if defined(__GTOS__) && defined(__i386__)
// Native scalar ABI types only. GTOS has no pthread ABI.
#include "../llvm-libc-types/size_t.h"
#include "../llvm-libc-types/ssize_t.h"
#include "../llvm-libc-types/off_t.h"
#include "../llvm-libc-types/pid_t.h"
#include "../llvm-libc-types/time_t.h"
#else
#include "../llvm-libc-types/__off64_t.h"
#include "../llvm-libc-types/__off_t.h"
#include "../llvm-libc-types/__uint64_t.h"
#include "../llvm-libc-types/blkcnt_t.h"
#include "../llvm-libc-types/blksize_t.h"
#include "../llvm-libc-types/caddr_t.h"
#include "../llvm-libc-types/clockid_t.h"
#include "../llvm-libc-types/dev_t.h"
#include "../llvm-libc-types/gid_t.h"
#include "../llvm-libc-types/id_t.h"
#include "../llvm-libc-types/ino64_t.h"
#include "../llvm-libc-types/ino_t.h"
#include "../llvm-libc-types/key_t.h"
#include "../llvm-libc-types/loff_t.h"
#include "../llvm-libc-types/mode_t.h"
#include "../llvm-libc-types/nlink_t.h"
#include "../llvm-libc-types/off64_t.h"
#include "../llvm-libc-types/off_t.h"
#include "../llvm-libc-types/pid_t.h"
#include "../llvm-libc-types/pthread_attr_t.h"
#include "../llvm-libc-types/pthread_cond_t.h"
#include "../llvm-libc-types/pthread_condattr_t.h"
#include "../llvm-libc-types/pthread_key_t.h"
#include "../llvm-libc-types/pthread_mutex_t.h"
#include "../llvm-libc-types/pthread_mutexattr_t.h"
#include "../llvm-libc-types/pthread_once_t.h"
#include "../llvm-libc-types/pthread_rwlock_t.h"
#include "../llvm-libc-types/pthread_rwlockattr_t.h"
#include "../llvm-libc-types/pthread_t.h"
#include "../llvm-libc-types/size_t.h"
#include "../llvm-libc-types/ssize_t.h"
#include "../llvm-libc-types/suseconds_t.h"
#include "../llvm-libc-types/time_t.h"
#include "../llvm-libc-types/timer_t.h"
#include "../llvm-libc-types/u_char.h"
#include "../llvm-libc-types/u_int.h"
#include "../llvm-libc-types/u_int16_t.h"
#include "../llvm-libc-types/u_int32_t.h"
#include "../llvm-libc-types/u_int8_t.h"
#include "../llvm-libc-types/u_long.h"
#include "../llvm-libc-types/u_short.h"
#include "../llvm-libc-types/uid_t.h"
#include "../llvm-libc-types/uint.h"
#include "../llvm-libc-types/ulong.h"
#include "../llvm-libc-types/useconds_t.h"
#include "../llvm-libc-types/ushort.h"

#endif // Native scalar types or original platform projection

#endif // _LLVM_LIBC_SYS_TYPES_H
