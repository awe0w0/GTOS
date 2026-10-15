/* GTOS IA32 configuration. SDK/runtime closure must pass separately. */
#ifndef GTOS_LIBXML_CONFIG_H
#define GTOS_LIBXML_CONFIG_H

#if !defined(__GTOS__) || !defined(__i386__)
#error This configuration requires the GTOS IA32 target
#endif

/* Do not advertise host getentropy, glob, mmap or dynamic loading. */
#define HAVE_DECL_GETENTROPY 0
#define HAVE_DECL_GLOB 0
#define HAVE_DECL_MMAP 0

/* These headers are provided by the pinned native C SDK/Clang resources. */
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1

#define PACKAGE "libxml2"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "libxml2"
#define PACKAGE_STRING "libxml2 2.16.0"
#define PACKAGE_TARNAME "libxml2"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "2.16.0"
#define VERSION "2.16.0"
#define XML_SYSCONFDIR "/etc"

/* Threading remains enabled by xmlversion.h. The required pthread, fd I/O
 * and ICU interfaces must actually compile/link; no single-thread fallback
 * or host ABI is selected here. XML_THREAD_LOCAL stays unset so the real
 * thread-specific state/destructor path is retained. */
#endif
