#ifndef __GTOS__MULTITASKING_H
#define __GTOS__MULTITASKING_H

#include <common/types.h>
#include <gdt.h>

namespace gtos {
    // Must match interruptstubs.s. esp/ss are present only on a privilege change;
    // a ring-0 interrupt frame ends at eflags. Initial tasks use those two words
    // as the bootstrap function's return-address and argument stack slots.
    struct CPUState {
        uint32_t eax, ebx, ecx, edx;
        uint32_t esi, edi, ebp;
        uint32_t vector, error;
        uint32_t eip, cs, eflags;
        uint32_t esp, ss;
    } __attribute__((packed));

    enum TaskState { TaskNew, TaskReady, TaskRunning, TaskSleeping, TaskTerminated };
    struct TaskStatistics {
        uint32_t runTicks;
        uint32_t dispatches;
        uint32_t yields;
        uint32_t sleeps;
    };
    class TaskManager;

    class Task {
        friend class TaskManager;
        private:
            uint8_t stack[4096] __attribute__((aligned(16)));
            CPUState* cpustate;
            void (*entrypoint)();
            TaskManager* owner;
            volatile TaskState state;
            uint32_t wakeTick;
            uint32_t affinityMask;
            TaskStatistics statistics;
            void Initialize(uint16_t codeSelector, void (*entry)());
            static void Bootstrap(Task* task) __attribute__((noreturn));
            Task(const Task&);
            Task& operator=(const Task&);
        public:
            Task(GlobalDescriptorTable* gdt, void entrypoint());
            Task(uint16_t codeSelector, void entrypoint());
            ~Task();
            TaskState State() const;
            TaskStatistics Statistics() const;
            uint32_t AffinityMask() const;
    };

    // BSP-only round robin. The pre-existing kernel/GUI context is always kept
    // as a runnable slot, so adding tasks cannot strand the main loop.
    class TaskManager {
        friend class Task;
        private:
            Task* tasks[256];
            int numTasks;
            int currentTask;
            CPUState* bootContext;
            volatile uint32_t ticks;
            uint32_t bootTicks;
            uint32_t switches;
            int IndexOf(Task* task) const;
            TaskManager(const TaskManager&);
            TaskManager& operator=(const TaskManager&);
        public:
            TaskManager();
            ~TaskManager();
            bool AddTask(Task* task);
            bool RemoveTask(Task* task);
            bool TerminateTask(Task* task);
            // Management request; a running task stops at the next timer IRQ.
            bool SleepTask(Task* task, uint32_t durationTicks);
            bool WakeTask(Task* task);
            // Only logical scheduler CPU 0 (the BSP) is online. Other masks fail.
            bool SetAffinity(Task* task, uint32_t cpuMask);
            // Synchronous task operations. Require interrupts enabled; durations
            // are PIT ticks, not milliseconds. Sleep(0) is a yield.
            bool SleepCurrent(uint32_t durationTicks);
            bool YieldCurrent();
            void ExitCurrent() __attribute__((noreturn));
            CPUState* Schedule(CPUState* cpustate);
            uint32_t Ticks() const;
            uint32_t BootTicks() const;
            uint32_t ContextSwitches() const;
            int TaskCount() const;
            Task* CurrentTask() const;
            uint32_t OnlineProcessors() const;
            // Uses a private scheduler and synthetic frames; never switches the
            // calling CPU or changes the live scheduler's task list.
            static bool RunSelfTests(GlobalDescriptorTable* gdt);
    };
}
#endif
