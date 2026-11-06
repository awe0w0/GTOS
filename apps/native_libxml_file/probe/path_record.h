#ifndef GTOS_NATIVE_PATH_RECORD_H
#define GTOS_NATIVE_PATH_RECORD_H
struct NativePathRecord {
  unsigned version, mode, stage, checks, failures, raw_calls;
};
static_assert(sizeof(NativePathRecord) == 24, "native path consumer layout");
#endif
