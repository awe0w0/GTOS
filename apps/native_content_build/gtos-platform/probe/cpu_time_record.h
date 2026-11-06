#ifndef GTOS_PERFETTO_CPU_TIME_RECORD_H
#define GTOS_PERFETTO_CPU_TIME_RECORD_H
namespace gtos_perfetto_cpu_probe {
constexpr unsigned RecordAddress = 0x40020000;
struct Record {
  unsigned version, mode, stage, checks, failures, cpu_queries, wall_queries;
  unsigned start_us, end_us, process_us, wall_us;
};
static_assert(sizeof(Record) == 44, "CPU clock consumer wire layout");
}
#endif
