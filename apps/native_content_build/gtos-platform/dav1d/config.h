// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if !defined(__GTOS__) || !defined(__i386__)
#error This configuration requires the native GTOS IA32 ABI.
#endif

// Target capabilities. The native runtime supplies posix_memalign, pthread
// synchronization and clock_gettime. Linux affinity, signals, dynamic loading
// and thread naming are not supplied by that runtime.
#define ARCH_AARCH64 0
#define ARCH_ARM 0
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH32 0
#define ARCH_LOONGARCH64 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_RV32 0
#define ARCH_RV64 0
#define ARCH_X86 1
#define ARCH_X86_32 1
#define ARCH_X86_64 0
#define CONFIG_16BPC 1
#define CONFIG_8BPC 1
#define ENDIANNESS_BIG 0
#define HAVE_ALIGNED_ALLOC 0
#define HAVE_ASM 1
#define HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_I8MM_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE2_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 0
#define HAVE_AS_ARCH_DIRECTIVE 0
#define HAVE_AS_FUNC 0
#define HAVE_C11_GENERIC 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_DLSYM 0
#define HAVE_DOTPROD 0
#define HAVE_ELF_AUX_INFO 0
#define HAVE_GETAUXVAL 0
#define HAVE_I8MM 0
#define HAVE_IO_H 0
#define HAVE_MEMALIGN 0
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_SETAFFINITY_NP 0
#define HAVE_PTHREAD_SETNAME_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0
#define HAVE_SIGACTION 0
#define HAVE_SVE 0
#define HAVE_SVE2 0
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define TRIM_DSP_FUNCTIONS 1
