# Configure the Chromium libxslt source subset from its own template.
cmake_minimum_required(VERSION 3.16)

foreach(input LIBXSLT_SRC GTOS_CLANG GTOS_HEADERS GTOS_OUTPUT)
  if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
    message(FATAL_ERROR "missing ${input}")
  endif()
endforeach()
if(EXISTS "${GTOS_OUTPUT}")
  message(FATAL_ERROR "refusing to reuse ${GTOS_OUTPUT}")
endif()
foreach(directory IN LISTS GTOS_HEADERS)
  if(NOT IS_DIRECTORY "${directory}")
    message(FATAL_ERROR "invalid target header directory ${directory}")
  endif()
  list(APPEND flags "-isystem${directory}")
endforeach()
file(STRINGS "${LIBXSLT_SRC}/libxslt/xsltconfig.h" version
     REGEX "^#define LIBXSLT_DOTTED_VERSION ")
if(NOT version MATCHES "^#define LIBXSLT_DOTTED_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\"$")
  message(FATAL_ERROR "unrecognized pinned xsltconfig.h version")
endif()
set(VERSION "${CMAKE_MATCH_1}")
set(PACKAGE "libxslt")
set(PACKAGE_NAME "${PACKAGE}")
set(PACKAGE_TARNAME "${PACKAGE}")
set(PACKAGE_STRING "${PACKAGE} ${VERSION}")
set(PACKAGE_VERSION "${VERSION}")
set(MODULE_EXTENSION "")
# Chromium's pinned public header enables profiling and disables modules/debugger.
set(WITH_PROFILER 1)

file(MAKE_DIRECTORY "${GTOS_OUTPUT}")
file(WRITE "${GTOS_OUTPUT}/probes.txt" "libxslt ${VERSION}\ncompile-only, GTOS IA32\n")
function(probe macro source)
  set(path "${GTOS_OUTPUT}/${macro}.c")
  file(WRITE "${path}" "${source}\n")
  execute_process(
    COMMAND "${GTOS_CLANG}" --target=i686-unknown-none-elf -ffreestanding
            -nostdinc -D__GTOS__=1 -Werror -fsyntax-only ${flags} "${path}"
    RESULT_VARIABLE rc ERROR_VARIABLE stderr)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${macro} failed (exit ${rc})\n${stderr}")
  endif()
  set(${macro} 1 PARENT_SCOPE)
  file(APPEND "${GTOS_OUTPUT}/probes.txt" "${macro}=1\n")
endfunction()

probe(HAVE_INTTYPES_H "#include <inttypes.h>")
probe(HAVE_LOCALE_H "#include <locale.h>")
probe(HAVE_SYS_STAT_H "#include <sys/stat.h>")
probe(HAVE_SYS_TYPES_H "#include <sys/types.h>")
probe(HAVE_UNISTD_H "#include <unistd.h>")
probe(HAVE_CLOCK_GETTIME "#include <time.h>
int probe(void) { struct timespec value; return clock_gettime(CLOCK_MONOTONIC, &value); }")
probe(HAVE_STAT "#include <sys/stat.h>
int probe(const char *path) { struct stat value; return stat(path, &value); }")
probe(HAVE_SNPRINTF "#include <stdio.h>
int probe(char *out) { return snprintf(out, 4, \"%d\", 7); }")
probe(HAVE_VSNPRINTF "#include <stdarg.h>
#include <stdio.h>
int probe(char *out, va_list args) { return vsnprintf(out, 4, \"%d\", args); }")

configure_file("${LIBXSLT_SRC}/config.h.cmake.in" "${GTOS_OUTPUT}/config.h" @ONLY)
message(STATUS "GTOS libxslt ${VERSION} config.h generated from nine SDK probes")
