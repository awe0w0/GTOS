#ifndef GTOS_PERFETTO_MEMORY_RECORD_H
#define GTOS_PERFETTO_MEMORY_RECORD_H
namespace gtos_perfetto_probe {
const unsigned RecordAddress = 0x40020000U;
struct Record {
  unsigned version, mode, stage, checks, failures, base, fault_address;
};
static_assert(sizeof(Record) == 28, "paged memory guest record");
}
#endif
