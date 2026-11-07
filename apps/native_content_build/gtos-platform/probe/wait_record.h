#ifndef GTOS_PERFETTO_WAIT_RECORD_H
#define GTOS_PERFETTO_WAIT_RECORD_H
namespace gtos_perfetto_wait_probe {
constexpr unsigned RecordAddress = 0x40020000;
struct Record { unsigned version, mode, stage, checks, failures, waits; };
static_assert(sizeof(Record) == 24, "wait consumer layout");
}
#endif
