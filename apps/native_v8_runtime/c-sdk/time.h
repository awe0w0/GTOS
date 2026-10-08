//===-- Standard C header <time.h> --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LLVM_LIBC_TIME_H
#define _LLVM_LIBC_TIME_H

#include "__llvm-libc-common.h"
#include "llvm-libc-macros/null-macro.h"
#include "llvm-libc-macros/time-macros.h"
#include "llvm-libc-types/clock_t.h"
#include "llvm-libc-types/clockid_t.h"
#include "llvm-libc-types/errno_t.h"
#include "llvm-libc-types/locale_t.h"
#include "llvm-libc-types/pid_t.h"
#include "llvm-libc-types/rsize_t.h"
#include "llvm-libc-types/size_t.h"
#include "llvm-libc-types/struct_itimerspec.h"
#include "llvm-libc-types/struct_sigevent.h"
#include "llvm-libc-types/struct_timespec.h"
#include "llvm-libc-types/struct_timeval.h"
#include "llvm-libc-types/struct_tm.h"
#include "llvm-libc-types/time_t.h"
#include "llvm-libc-types/timer_t.h"
#include "llvm-libc-types/tm_gmtoff_t.h"

__BEGIN_C_DECLS

char *asctime(const struct tm *) __NOEXCEPT;

char *asctime_r(const struct tm *, char *) __NOEXCEPT;

clock_t clock(void) __NOEXCEPT;

int clock_getcpuclockid(pid_t, clockid_t *) __NOEXCEPT;

int clock_gettime(clockid_t, struct timespec *) __NOEXCEPT;

int clock_settime(clockid_t, const struct timespec *) __NOEXCEPT;

char *ctime(const time_t *) __NOEXCEPT;

char *ctime_r(const time_t *, char *) __NOEXCEPT;

double difftime(time_t, time_t) __NOEXCEPT;

struct tm *gmtime(const time_t *) __NOEXCEPT;

struct tm *gmtime_r(const time_t *, struct tm *) __NOEXCEPT;

struct tm *localtime(const time_t *) __NOEXCEPT;

struct tm *localtime_r(const time_t *, struct tm *) __NOEXCEPT;

time_t mktime(struct tm *) __NOEXCEPT;

int nanosleep(const struct timespec *, struct timespec *) __NOEXCEPT;

size_t strftime(char *__restrict, size_t, const char *__restrict, const struct tm *__restrict) __NOEXCEPT;

size_t strftime_l(char *__restrict, size_t, const char *__restrict, const struct tm *__restrict, locale_t) __NOEXCEPT;

char *strptime(const char *__restrict, const char *__restrict, struct tm *__restrict) __NOEXCEPT;

time_t time(time_t *) __NOEXCEPT;

int timer_create(clockid_t, struct sigevent *__restrict, timer_t *__restrict) __NOEXCEPT;

int timer_delete(timer_t) __NOEXCEPT;

int timer_gettime(timer_t, struct itimerspec *) __NOEXCEPT;

int timer_settime(timer_t, int, const struct itimerspec *__restrict, struct itimerspec *__restrict) __NOEXCEPT;

int timespec_get(struct timespec *, int) __NOEXCEPT;

void tzset(void) __NOEXCEPT;

extern int daylight;
extern long timezone;
extern char * tzname[2];

__END_C_DECLS

#endif // _LLVM_LIBC_TIME_H
