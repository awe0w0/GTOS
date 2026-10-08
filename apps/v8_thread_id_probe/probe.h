#ifndef GTOS_V8_THREAD_ID_PROBE_H
#define GTOS_V8_THREAD_ID_PROBE_H
#include "process/abi.h"
#include "emutls.h"
#include "../native_heap/heap.h"
#include "../native_thread_id_probe/record.h"
#ifndef GTOS_TLS_PROBE_MODE
#define GTOS_TLS_PROBE_MODE 0
#endif
extern "C" volatile NativeTlsProbeRecord native_tls_record;
extern "C" int tls_probe_call(unsigned operation, unsigned first, unsigned second);
extern "C" [[noreturn]] void tls_probe_fail(unsigned code);
#endif
