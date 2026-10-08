#ifndef GTOS_COMPILER_TLS_CALLS_H
#define GTOS_COMPILER_TLS_CALLS_H
extern "C" unsigned char* actual_tls_zero();
extern "C" unsigned char* actual_tls_nonzero();
extern "C" unsigned char* actual_tls_aligned();
extern "C" unsigned char* actual_tls_oom();
#endif
