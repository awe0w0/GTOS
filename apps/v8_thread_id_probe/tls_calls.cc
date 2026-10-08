#include "tls_calls.h"
alignas(64) thread_local unsigned char tls_probe_zero[257]{};
alignas(16) thread_local unsigned char tls_probe_nonzero[64] = {0xA5, 0x69, 0x37};
alignas(4096) thread_local unsigned char tls_probe_aligned[64] = {0xC4, 0x1B, 0xA9};
alignas(16) thread_local unsigned char tls_probe_oom[65504]{};
extern "C" __attribute__((noinline)) unsigned char* actual_tls_zero() { return tls_probe_zero; }
extern "C" __attribute__((noinline)) unsigned char* actual_tls_nonzero() { return tls_probe_nonzero; }
extern "C" __attribute__((noinline)) unsigned char* actual_tls_aligned() { return tls_probe_aligned; }
extern "C" __attribute__((noinline)) unsigned char* actual_tls_oom() { return tls_probe_oom; }
