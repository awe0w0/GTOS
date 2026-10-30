# GTOS libxml2 configuration generator (CMake), for Chromium's `//third_party/libxml:libxml`.
#
# Why CMake and not `./configure`:
#   The vendored checkout at $LIBXML_SRC ships `configure.ac` but no generated `configure`, and this host has no autoconf,
#   automake, aclocal, libtool or even m4 (measured with `command -v`; the run log prints the result). CMake 3.16.3 is present and
#   libxml2 2.16.0 is a version that supports CMake upstream, so the *upstream templates* this checkout still carries —
#   `config.h.cmake.in` and `include/libxml/xmlversion.h.in` — are the inputs used here.
#
# What this is, stated plainly because r20 was reviewed for overclaiming it: this is this project's CMakeLists driving upstream
# templates with probes the compiler really ran. It is NOT the upstream CMakeLists and NOT the upstream `configure`: the feature
# answers come from the contract table in libxml_profile.cmake (what upstream and Chromium declare, what the build actually compiles,
# what the target can do), not from autoconf's own detection order.
#
# What is measured and what is not:
#   * compiled source set: read from the real `//third_party/libxml:libxml` target by libxml_parse_target_sources() — one target's
#     `sources` lists, not every quoted path in the file, and not a per-OS branch that cannot be true for GTOS;
#   * compile capability (headers, declarations, attributes, TLS) — measured here with the GTOS clang and the GTOS SDK includes;
#   * link capability — NOT measured here. Probes use CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY and never claim a symbol links;
#   * run semantics — measured by consumers that link and execute, never here.

cmake_minimum_required(VERSION 3.16)

# Everything the compiler test needs is in place before project(): CMake identifies the C compiler during it, and this target
# cannot link, so the probe type and the target flags are set first. The -D values are cache entries, so they are already here.
foreach(required LIBXML_SRC GTOS_SDK_DIR GTOS_LIBCXX_INCLUDE GTOS_CLANG_INCLUDE LIBXML_BUILD_GN)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is not set; this generator does not guess where the pinned sources are")
  endif()
endforeach()

# A feature the contract enables but the SDK cannot provide has to be named here, so a reduction is a recorded decision instead of a
# silent 0 that a later reader would mistake for "libxml2 has no such feature".
if(NOT DEFINED LIBXML_DOWNGRADES)
  set(LIBXML_DOWNGRADES "WITH_ICU;WITH_THREADS" CACHE STRING
      "contract features this configuration knowingly turns off because the SDK cannot provide their capability")
endif()

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
message(STATUS "  downgrades: ${LIBXML_DOWNGRADES}")

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
check_include_file(unicode/utypes.h HAVE_ICU_HEADER)

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

# ---------------------------------------------------------------- the compiled source set, from the real GN target
include("${CMAKE_CURRENT_LIST_DIR}/libxml_profile.cmake")
libxml_parse_target_sources("${LIBXML_BUILD_GN}" "libxml" libxml_sources libxml_other_sources libxml_skipped_sources)
list(LENGTH libxml_sources libxml_source_count)
message(STATUS "  compiled sources: ${libxml_source_count} libxml .c files")
foreach(source IN LISTS libxml_sources)
  message(STATUS "    ${source}")
endforeach()
foreach(other IN LISTS libxml_other_sources)
  message(STATUS "    (non-libxml source in the same target: ${other})")
endforeach()

# ---------------------------------------------------------------- the feature contract
# A source list only says which files compile. Whether a *feature* is on is a separate question: pattern.c and valid.c are compiled
# while Chromium declares those features off (their bodies are behind LIBXML_*_ENABLED), and SAX1 is declared on while its own SAX.c
# is deleted — the API lives in SAX2.c. The contract table records all of that, and the resolution below refuses to guess.
set(libxml_capabilities
    "icu=${HAVE_ICU_HEADER}"
    "threads=${HAVE_PTHREAD_H}"
    "iconv=${HAVE_ICONV_H}"
    "zlib=${HAVE_ZLIB_H}"
    "lzma=0"
    "modules=0")
libxml_resolve_contract("${libxml_sources}" "${libxml_capabilities}" "${LIBXML_DOWNGRADES}")

foreach(with_var WITH_C14N WITH_CATALOG WITH_DEBUG WITH_HTML WITH_HTTP WITH_ICONV WITH_ICU WITH_ISO8859X WITH_MODULES
        WITH_OUTPUT WITH_PATTERN WITH_PUSH WITH_READER WITH_REGEXPS WITH_RELAXNG WITH_SAX1 WITH_SCHEMAS WITH_SCHEMATRON
        WITH_THREADS WITH_VALID WITH_WINPATH WITH_WRITER WITH_XINCLUDE WITH_XPATH WITH_XPTR WITH_ZLIB)
  message(STATUS "  ${with_var}=${${with_var}}  (${${with_var}_REASON})")
endforeach()
# Not in the contract table because no template asks for it, but xmlversion.h uses it.
set(WITH_THREAD_ALLOC 0)
set(WITH_LEGACY 0)

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

# Probe results next to the headers, so the profile can be read back without rerunning the generator. The layers are kept apart on
# purpose: what compiles, what the contract asks for, what this configuration honours, and what was downgraded and why.
set(probe_text "version ${VERSION}\nversion_number ${LIBXML_VERSION_NUMBER}\n")
string(APPEND probe_text "--- capabilities (compile probes, no link)\n")
string(APPEND probe_text "HAVE_STDINT_H ${HAVE_STDINT_H}\nHAVE_DLFCN_H ${HAVE_DLFCN_H}\nHAVE_PTHREAD_H ${HAVE_PTHREAD_H}\n")
string(APPEND probe_text "HAVE_ZLIB_H ${HAVE_ZLIB_H}\nHAVE_ICONV_H ${HAVE_ICONV_H}\nHAVE_ICU_HEADER ${HAVE_ICU_HEADER}\n")
string(APPEND probe_text "HAVE_READLINE_HEADER ${HAVE_READLINE_HEADER}\nHAVE_HISTORY_HEADER ${HAVE_HISTORY_HEADER}\n")
string(APPEND probe_text "HAVE_DECL_MMAP ${HAVE_DECL_MMAP}\nHAVE_DECL_GLOB ${HAVE_DECL_GLOB}\n")
string(APPEND probe_text "HAVE_DECL_GETENTROPY ${HAVE_DECL_GETENTROPY}\n")
string(APPEND probe_text "HAVE_FUNC_ATTRIBUTE_DESTRUCTOR ${HAVE_FUNC_ATTRIBUTE_DESTRUCTOR}\n")
string(APPEND probe_text "HAVE_TLS_KEYWORD ${HAVE_TLS_KEYWORD}\nXML_THREAD_LOCAL ${XML_THREAD_LOCAL}\nXML_SYSCONFDIR ${XML_SYSCONFDIR}\n")
string(APPEND probe_text "HAVE_DLOPEN ${HAVE_DLOPEN}\nHAVE_SHLLOAD ${HAVE_SHLLOAD}\n")
string(APPEND probe_text "HAVE_LIBREADLINE ${HAVE_LIBREADLINE}\nHAVE_LIBHISTORY ${HAVE_LIBHISTORY}\n")
string(APPEND probe_text "--- compiled sources (from //third_party/libxml:libxml, count ${libxml_source_count})\n")
foreach(source IN LISTS libxml_sources)
  string(APPEND probe_text "source ${source}\n")
endforeach()
foreach(other IN LISTS libxml_other_sources)
  string(APPEND probe_text "other_source ${other}\n")
endforeach()
string(APPEND probe_text "--- honoured features (what the generated header may declare)\n")
foreach(with_var WITH_C14N WITH_CATALOG WITH_DEBUG WITH_HTML WITH_HTTP WITH_ICONV WITH_ICU WITH_ISO8859X WITH_MODULES
        WITH_OUTPUT WITH_PATTERN WITH_PUSH WITH_READER WITH_REGEXPS WITH_RELAXNG WITH_SAX1 WITH_SCHEMAS WITH_SCHEMATRON
        WITH_THREADS WITH_THREAD_ALLOC WITH_VALID WITH_WINPATH WITH_WRITER WITH_XINCLUDE WITH_XPATH WITH_XPTR WITH_ZLIB)
  string(APPEND probe_text "${with_var} ${${with_var}}\n")
endforeach()
string(APPEND probe_text "--- downgrades requested explicitly\n")
foreach(feature IN LISTS LIBXML_DOWNGRADES)
  string(APPEND probe_text "downgrade ${feature} (${${feature}_REASON})\n")
endforeach()
string(APPEND probe_text "--- why each feature is what it is\n")
foreach(with_var WITH_C14N WITH_CATALOG WITH_DEBUG WITH_HTML WITH_HTTP WITH_ICONV WITH_ICU WITH_ISO8859X WITH_MODULES
        WITH_OUTPUT WITH_PATTERN WITH_PUSH WITH_READER WITH_REGEXPS WITH_RELAXNG WITH_SAX1 WITH_SCHEMAS WITH_SCHEMATRON
        WITH_THREADS WITH_VALID WITH_WINPATH WITH_WRITER WITH_XINCLUDE WITH_XPATH WITH_XPTR WITH_ZLIB)
  string(APPEND probe_text "reason ${with_var} ${${with_var}_REASON}\n")
endforeach()
file(WRITE "${out_dir}/probes.txt" "${probe_text}")
message(STATUS "wrote ${out_dir}/config.h, ${out_dir}/include/libxml/xmlversion.h and ${out_dir}/probes.txt")
