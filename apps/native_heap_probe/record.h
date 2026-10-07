#ifndef GTOS_NATIVE_HEAP_PROBE_RECORD_H
#define GTOS_NATIVE_HEAP_PROBE_RECORD_H
// Independent guest acceptance record; all values are user virtual addresses.
// No allocator header is exposed or inspected by the kernel oracle.
struct HeapProbeRecord {
    unsigned version, mode, stage, base, handle;
    unsigned primary, primaryBytes, neighbor, neighborBytes, checks;
};
static_assert(sizeof(HeapProbeRecord) == 40, "Heap probe record wire");
#endif
