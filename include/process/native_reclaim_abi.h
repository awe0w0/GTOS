#ifndef GTOS_PROCESS_NATIVE_RECLAIM_ABI_H
#define GTOS_PROCESS_NATIVE_RECLAIM_ABI_H

// Diagnostic-only proof words shared by the reclaim kernel variant and its two
// diagnostic CPL3 payloads. They are NOT part of the native ABI: no syscall reads
// or writes them, and no production image contains either side. They exist so the
// kernel can tell "the payload really owned a live window with an unfinished
// draft" apart from "the payload never got that far", without trusting a log line.
#define GTOS_RECLAIM_MAGIC       0x57524C4DU  // "WRLM": window live, draft present
#define GTOS_RECLAIM_QUEUE_MAGIC 0x57514C51U  // "WQLQ": the same, plus pending input
#endif
