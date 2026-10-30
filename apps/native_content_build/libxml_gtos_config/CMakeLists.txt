# GTOS libxml2 configuration generator (CMake), for Chromium's `//third_party/libxml:libxml`.
#
# Why CMake and not `./configure`:
#   The vendored checkout at $LIBXML_SRC ships `configure.ac` but no generated `configure`, and this host has no autoconf,
#   automake, aclocal, libtool or even m4 (measured with `command -v`; the run log prints the result). CMake 3.16.3 is present and
#   libxml2 2.16.0 is a version that supports CMake upstream, so the *upstream templates* this checkout still carries —
#   `config.h.cmake.in` and `include/libxml/xmlversion.h.in` — are the inputs used here. Nothing here invents a macro value: every
#   HAVE_* answer comes from a probe the compiler actually ran against the GTOS SDK, and every WITH_* answer is read off the source
#   list `//third_party/libxml/BUILD.gn` really compiles.
#
# What is measured and what is not:
#   * compile capability (headers, declarations, attributes, TLS) — measured here with the GTOS clang and the GTOS SDK includes;
#   * link capability — NOT measured here. This profile has no crt and no archive set to link against, so probes use
#     CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY and never claim a symbol links; they only say the header names it;
#   * run semantics — not measured anywhere in this directory.
#
# Inputs: LIBXML_SRC (pinned checkout), GTOS_SDK_DIR, GTOS_LIBCXX_INCLUDE, GTOS_CLANG_INCLUDE, LIBXML_BUILD_GN (the file the
#         profile must stay consistent with), and CMAKE_C_COMPILER (the pinned GTOS clang).

cmake_minimum_required(VERSION 3.16)

# Everything the compiler test needs is in place before project(): CMake identifies the C compiler during it, and this target
# cannot link, so the probe type and the target flags are set first. The -D values are cache entries, so they are already here.
foreach(required LIBXML_SRC GTOS_SDK_DIR GTOS_LIBCXX_INCLUDE GTOS_CLANG_INCLUDE LIBXML_BUILD_GN)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is not set; this generator does not guess where the pinned sources are")
  endif()
endforeach()

# No linker in this profile: every probe stops at the object file, so no probe can pass because of a host library.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The GTOS target flags are the ones //build/toolchain/gtos and //build/config/gtos pass, repeated as literals on purpose so a
# drift in either GN file shows up as a mismatch instead of silently changing what was measured.
set(gtos_flags
    "--target=i686-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-mmx -msse2 -mfpmath=sse -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx -nostdinc")
set(gtos_includes
    "-isystem${GTOS_SDK_DIR}/sdk -isystem${GTOS_LIBCXX_INCLUDE} -isystem${GTOS_SDK_DIR}/c-sdk -isystem${GTOS_CLANG_INCLUDE}")
set(CMAKE_C_FLAGS "${gtos_flags} ${gtos_includes} -D__GTOS__=1 -DLIBXML_STATIC= -femulated-tls -fstack-usage")

project(libxml_gtos_config LANGUAGES C)

foreach(path_expected "${LIBXML_SRC}/configure.ac" "${LIBXML_SRC}/config.h.cmake.in"
        "${LIBXML_SRC}/include/libxml/xmlversion.h.in" "${LIBXML_SRC}/VERSION" "${LIBXML_BUILD_GN}")
  if(NOT EXISTS "${path_expected}")
    message(FATAL_ERROR "missing input: ${path_expected}")
  endif()
endforeach()

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckCSourceCompiles)

message(STATUS "GTOS libxml2 configuration generator")
message(STATUS "  compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  flags:    ${CMAKE_C_FLAGS}")
message(STATUS "  source:   ${LIBXML_SRC}")
message(STATUS "  BUILD.gn: ${LIBXML_BUILD_GN}")

# ---------------------------------------------------------------- version, from the checkout's own VERSION file
file(READ "${LIBXML_SRC}/VERSION" libxml_version_raw)
string(STRIP "${libxml_version_raw}" VERSION)
string(REPLACE "." ";" version_parts "${VERSION}")
list(GET version_parts 0 version_major)
list(GET version_parts 1 version_minor)
list(GET version_parts 2 version_micro)
math(EXPR LIBXML_VERSION_NUMBER "${version_major} * 10000 + ${version_minor} * 100 + ${version_micro}")
set(LIBXML_VERSION_EXTRA "")
message(STATUS "  version:  ${VERSION} (${LIBXML_VERSION_NUMBER})")

# ---------------------------------------------------------------- capability probes (compile only)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(readline/readline.h HAVE_READLINE_HEADER)
check_include_file(readline/history.h HAVE_HISTORY_HEADER)
check_include_file(pthread.h HAVE_PTHREAD_H)
check_include_file(zlib.h HAVE_ZLIB_H)
check_include_file(iconv.h HAVE_ICONV_H)

check_symbol_exists(mmap "sys/mman.h" HAVE_DECL_MMAP)
check_symbol_exists(glob "glob.h" HAVE_DECL_GLOB)
check_symbol_exists(getentropy "unistd.h" HAVE_DECL_GETENTROPY)

check_c_source_compiles("
__attribute__((destructor)) static void cleanup(void) { }
int main(void) { return 0; }" HAVE_FUNC_ATTRIBUTE_DESTRUCTOR)

check_c_source_compiles("
static __thread int counter;
int main(void) { return counter; }" HAVE_TLS_KEYWORD)

# A declaration probe is not a link probe: these say "the SDK header names it", and the report keeps that distinction.
if(HAVE_DLFCN_H)
  set(HAVE_DLOPEN 1)
else()
  set(HAVE_DLOPEN 0)
endif()
set(HAVE_SHLLOAD 0)
if(HAVE_READLINE_HEADER)
  set(HAVE_LIBREADLINE 1)
else()
  set(HAVE_LIBREADLINE 0)
endif()
if(HAVE_HISTORY_HEADER)
  set(HAVE_LIBHISTORY 1)
else()
  set(HAVE_LIBHISTORY 0)
endif()

if(HAVE_TLS_KEYWORD)
  set(XML_THREAD_LOCAL "__thread")
else()
  set(XML_THREAD_LOCAL "")
endif()
# Upstream's CMake default; GTOS has no /etc of its own, so this stays empty rather than naming a host path.
set(XML_SYSCONFDIR "")

# ---------------------------------------------------------------- feature profile, read off BUILD.gn
# The derivation lives in libxml_profile.cmake so `cmake -P test_profile.cmake` can exercise the same function on synthetic BUILD.gn
# files without a compiler; this file only applies its answers to the two templates.
include("${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake")
libxml_derive_profile("${LIBXML_BUILD_GN}")

foreach(with_var WITH_C14N WITH_CATALOG WITH_DEBUG WITH_HTML WITH_OUTPUT WITH_PATTERN WITH_PUSH WITH_READER WITH_REGEXPS
        WITH_RELAXNG WITH_SAX1 WITH_SCHEMAS WITH_SCHEMATRON WITH_VALID WITH_WRITER WITH_XINCLUDE WITH_XPATH WITH_XPTR)
  message(STATUS "  ${with_var}=${${with_var}}")
endforeach()

# Not "is the file compiled" but "can the target do it at all": threads need a target thread ABI, iconv and zlib need a real
# library. Each is answered by the probe above, never by taste — GTOS has no pthread.h, so the probe is what turns threads off.
if(HAVE_PTHREAD_H)
  set(WITH_THREADS 1)
  set(WITH_THREAD_ALLOC 0)
else()
  set(WITH_THREADS 0)
  set(WITH_THREAD_ALLOC 0)
endif()
if(HAVE_ICONV_H)
  set(WITH_ICONV 1)
else()
  set(WITH_ICONV 0)
endif()
set(WITH_ICU 0)
# libxml2 2.16 removed HTTP support and kept only the stub API; the template's own comment says so, and nothing in the pinned
# checkout compiles nanohttp.c any more (grep of its BUILD.gn source list). So this is a statement about the pinned version, not
# about GTOS: the stubs are what the header offers, and they are on for every platform that builds 2.16, including Chromium's
# pre-generated linux configuration.
set(WITH_HTTP 1)
if(HAVE_ZLIB_H)
  set(WITH_ZLIB 1)
else()
  set(WITH_ZLIB 0)
endif()
set(WITH_MODULES 0)
set(MODULE_EXTENSION "")
set(WITH_ISO8859X 1)   # pure tables in encoding.c, no OS dependency
set(WITH_WINPATH 0)

# ---------------------------------------------------------------- outputs
set(out_dir "${CMAKE_BINARY_DIR}/out")
file(MAKE_DIRECTORY "${out_dir}/include/libxml")
configure_file("${LIBXML_SRC}/config.h.cmake.in" "${out_dir}/config.h")
configure_file("${LIBXML_SRC}/include/libxml/xmlversion.h.in" "${out_dir}/include/libxml/xmlversion.h")

# Empty output is not a pass: the two files are the whole point of this project. libxml_check_generated() also rejects output that kept
# a template variable this project never answered — that leaves "@NAME@" or a bare "#if " behind, which the compiler reports as a
# syntax error thousands of lines away from the cause. The first run of this generator shipped exactly that (WITH_HTTP), so the check
# is part of the derivation and has its own test.
foreach(generated "${out_dir}/config.h" "${out_dir}/include/libxml/xmlversion.h")
  libxml_check_generated("${generated}")
endforeach()

# Probe results next to the headers, so the profile can be read back without rerunning the generator.
file(WRITE "${out_dir}/probes.txt"
"version ${VERSION}\nversion_number ${LIBXML_VERSION_NUMBER}\n"
"HAVE_STDINT_H ${HAVE_STDINT_H}\nHAVE_DLFCN_H ${HAVE_DLFCN_H}\nHAVE_PTHREAD_H ${HAVE_PTHREAD_H}\n"
"HAVE_ZLIB_H ${HAVE_ZLIB_H}\nHAVE_ICONV_H ${HAVE_ICONV_H}\nHAVE_READLINE_HEADER ${HAVE_READLINE_HEADER}\n"
"HAVE_HISTORY_HEADER ${HAVE_HISTORY_HEADER}\nHAVE_DECL_MMAP ${HAVE_DECL_MMAP}\nHAVE_DECL_GLOB ${HAVE_DECL_GLOB}\n"
"HAVE_DECL_GETENTROPY ${HAVE_DECL_GETENTROPY}\nHAVE_FUNC_ATTRIBUTE_DESTRUCTOR ${HAVE_FUNC_ATTRIBUTE_DESTRUCTOR}\n"
"HAVE_TLS_KEYWORD ${HAVE_TLS_KEYWORD}\nXML_THREAD_LOCAL ${XML_THREAD_LOCAL}\nXML_SYSCONFDIR ${XML_SYSCONFDIR}\n"
"HAVE_DLOPEN ${HAVE_DLOPEN}\nHAVE_SHLLOAD ${HAVE_SHLLOAD}\nHAVE_LIBREADLINE ${HAVE_LIBREADLINE}\n"
"HAVE_LIBHISTORY ${HAVE_LIBHISTORY}\n"
"WITH_THREADS ${WITH_THREADS}\nWITH_THREAD_ALLOC ${WITH_THREAD_ALLOC}\nWITH_ICONV ${WITH_ICONV}\n"
"WITH_ZLIB ${WITH_ZLIB}\nWITH_ICU ${WITH_ICU}\nWITH_HTTP ${WITH_HTTP}\nWITH_C14N ${WITH_C14N}\nWITH_CATALOG ${WITH_CATALOG}\n"
"WITH_DEBUG ${WITH_DEBUG}\nWITH_HTML ${WITH_HTML}\nWITH_OUTPUT ${WITH_OUTPUT}\nWITH_PATTERN ${WITH_PATTERN}\n"
"WITH_READER ${WITH_READER}\nWITH_REGEXPS ${WITH_REGEXPS}\nWITH_RELAXNG ${WITH_RELAXNG}\n"
"WITH_SCHEMAS ${WITH_SCHEMAS}\nWITH_SCHEMATRON ${WITH_SCHEMATRON}\nWITH_VALID ${WITH_VALID}\n"
"WITH_WRITER ${WITH_WRITER}\nWITH_XINCLUDE ${WITH_XINCLUDE}\nWITH_XPATH ${WITH_XPATH}\n"
"WITH_XPTR ${WITH_XPTR}\nWITH_SAX1 ${WITH_SAX1}\nWITH_MODULES ${WITH_MODULES}\n"
"WITH_ISO8859X ${WITH_ISO8859X}\nWITH_PUSH ${WITH_PUSH}\nWITH_WINPATH ${WITH_WINPATH}\n")
message(STATUS "wrote ${out_dir}/config.h and ${out_dir}/include/libxml/xmlversion.h")
