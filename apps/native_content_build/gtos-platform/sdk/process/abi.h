#ifndef __GTOS__PROCESS__ABI_H
#define __GTOS__PROCESS__ABI_H
// Experimental GTOS i386 ABI v1; not Linux compatible. int 0x80: EAX=call,
// EBX=arg1, ECX=arg2. EAX=result; all other GPRs and segment registers preserved.
// Negative signed 32-bit results are errors. Integer instructions only, no TLS.
#define GTOS_NATIVE_ABI_VERSION 1U
#define GTOS_SYS_ABI 0x4700U
#define GTOS_SYS_WRITE 0x4701U
#define GTOS_SYS_TICKS 0x4702U
#define GTOS_SYS_YIELD 0x4703U
#define GTOS_SYS_EXIT 0x4704U
#define GTOS_NATIVE_WRITE_LIMIT 256U
#define GTOS_ERR_UNSUPPORTED (-38)
#define GTOS_ERR_BAD_ADDRESS (-14)
#define GTOS_ERR_TOO_LARGE (-7)
#define GTOS_ERR_BAD_STATE (-22)
#endif
