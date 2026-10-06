// Compile both ways to verify a self-contained header and include-order safety.
#ifdef GTOS_QUEUE_INCLUDE_FIRST
#include <hardwarecommunication/cpu_work_queue.h>
#include <hardwarecommunication/cpu_startup.h>
#include <multitasking.h>
#else
#include <multitasking.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_work_queue.h>
#endif
#include <hardwarecommunication/cpu_work_queue.h>

static_assert(gtos::hardwarecommunication::CpuWorkQueue::Capacity == 16, "fixed capacity");
static_assert(gtos::hardwarecommunication::CpuWorkQueue::MaxIterations == 65536, "bounded work");
