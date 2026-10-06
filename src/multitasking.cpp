#include <multitasking.h>

using namespace gtos;

namespace {
    uint32_t DisableInterrupts() {
#ifdef GTOS_CPU_TEST
        return 0x202;
#else
        uint32_t flags;
        asm volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
        return flags;
#endif
    }
    void RestoreInterrupts(uint32_t flags) {
#ifndef GTOS_CPU_TEST
        // Restore only IF; do not expose compiler arithmetic flags to popfl.
        if (flags & 0x200) asm volatile("sti" : : : "memory");
#else
        (void)flags;
#endif
    }
    void WaitForInterrupt() {
#ifndef GTOS_CPU_TEST
        // STI's interrupt shadow closes the interrupt-before-HLT race.
        asm volatile("sti; hlt; cli" : : : "memory");
#endif
    }
    class InterruptGuard {
        uint32_t flags;
    public:
        InterruptGuard() : flags(DisableInterrupts()) {}
        ~InterruptGuard() { RestoreInterrupts(flags); }
    };
    void SelfTestEntry() {}
}

void Task::Initialize(uint16_t codeSelector, void (*entry)()) {
    // The frame plus 12 padding bytes yields ESP % 16 == 12 on entry to
    // Bootstrap. Ring-0 IRET consumes the frame except its final esp/ss slots.
    cpustate = (CPUState*)(stack + sizeof(stack) - sizeof(CPUState) - 12);
    cpustate->eax = cpustate->ebx = cpustate->ecx = cpustate->edx = 0;
    cpustate->esi = cpustate->edi = cpustate->ebp = 0;
    cpustate->vector = cpustate->error = 0;
    cpustate->eip = (uint32_t)&Task::Bootstrap;
    cpustate->cs = codeSelector;
    cpustate->eflags = 0x202;
    cpustate->esp = 0; // Bootstrap is noreturn; synthetic return address.
    cpustate->ss = (uint32_t)this; // First cdecl argument to Bootstrap.
    entrypoint = entry;
    owner = 0;
    state = TaskNew;
    wakeTick = 0;
    affinityMask = 1;
    statistics.runTicks = statistics.dispatches = 0;
    statistics.yields = statistics.sleeps = 0;
}

Task::Task(GlobalDescriptorTable* gdt, void entry()) {
    Initialize(gdt ? gdt->CodeSegmentSelector() : 0, entry);
}
Task::Task(uint16_t codeSelector, void entry()) { Initialize(codeSelector, entry); }
Task::~Task() {
    // Lifetime is caller-owned: remove a non-running task before destroying it.
}
void Task::Bootstrap(Task* task) {
    task->entrypoint();
    task->owner->ExitCurrent();
}
TaskState Task::State() const { return state; }
TaskStatistics Task::Statistics() const { InterruptGuard guard; return statistics; }
uint32_t Task::AffinityMask() const { return affinityMask; }

TaskManager::TaskManager() : numTasks(0), currentTask(-1), bootContext(0),
    ticks(0), bootTicks(0), switches(0) {
    for (int i = 0; i < 256; ++i) tasks[i] = 0;
}
TaskManager::~TaskManager() {}
int TaskManager::IndexOf(Task* task) const {
    for (int i = 0; i < numTasks; ++i) if (tasks[i] == task) return i;
    return -1;
}
bool TaskManager::AddTask(Task* task) {
    InterruptGuard guard;
    if (!task || numTasks == 256 || task->owner || task->state != TaskNew
        || !task->entrypoint || !task->cpustate->cs) return false;
    tasks[numTasks++] = task;
    task->owner = this;
    task->state = TaskReady;
    return true;
}
bool TaskManager::RemoveTask(Task* task) {
    InterruptGuard guard;
    int index = IndexOf(task);
    if (index < 0 || index == currentTask) return false;
    for (int i = index; i + 1 < numTasks; ++i) tasks[i] = tasks[i + 1];
    tasks[--numTasks] = 0;
    if (currentTask > index) --currentTask;
    task->owner = 0;
    task->state = TaskTerminated;
    return true;
}
bool TaskManager::TerminateTask(Task* task) {
    InterruptGuard guard;
    if (IndexOf(task) < 0 || task->state == TaskTerminated) return false;
    task->state = TaskTerminated;
    return true;
}
bool TaskManager::SleepTask(Task* task, uint32_t durationTicks) {
    InterruptGuard guard;
    // Half-range bound makes deadline comparisons safe across tick rollover.
    if (IndexOf(task) < 0 || !durationTicks || durationTicks > 0x7FFFFFFFU
        || (task->state != TaskReady && task->state != TaskRunning)) return false;
    task->wakeTick = ticks + durationTicks;
    task->state = TaskSleeping;
    ++task->statistics.sleeps;
    return true;
}
bool TaskManager::WakeTask(Task* task) {
    InterruptGuard guard;
    if (IndexOf(task) < 0 || task->state != TaskSleeping) return false;
    task->state = TaskReady;
    return true;
}
bool TaskManager::SetAffinity(Task* task, uint32_t cpuMask) {
    InterruptGuard guard;
    if (IndexOf(task) < 0 || cpuMask != 1 || task->state == TaskTerminated) return false;
    task->affinityMask = cpuMask;
    return true;
}
bool TaskManager::SleepCurrent(uint32_t durationTicks) {
    if (!durationTicks) return YieldCurrent();
    uint32_t flags = DisableInterrupts();
    Task* task = CurrentTask();
    if (!(flags & 0x200) || !task || !SleepTask(task, durationTicks)) {
        RestoreInterrupts(flags);
        return false;
    }
    while (task->state == TaskSleeping) WaitForInterrupt();
    RestoreInterrupts(flags);
    return true;
}
bool TaskManager::YieldCurrent() {
    uint32_t flags = DisableInterrupts();
    Task* task = CurrentTask();
    if (!(flags & 0x200)) { RestoreInterrupts(flags); return false; }
    uint32_t before = task ? task->statistics.dispatches : ticks;
    if (task) ++task->statistics.yields;
    do { WaitForInterrupt(); }
    while (task ? task->statistics.dispatches == before : ticks == before);
    RestoreInterrupts(flags);
    return true;
}
void TaskManager::ExitCurrent() {
    DisableInterrupts();
    Task* task = CurrentTask();
    if (task) task->state = TaskTerminated;
    for (;;) WaitForInterrupt();
}

CPUState* TaskManager::Schedule(CPUState* cpustate) {
    // Called once per PIT IRQ with IF clear, never by another processor.
    if (!cpustate) return cpustate;
    ++ticks;
    int previous = currentTask;
    if (currentTask >= 0) {
        Task* task = tasks[currentTask];
        task->cpustate = cpustate;
        ++task->statistics.runTicks;
        if (task->state == TaskRunning) task->state = TaskReady;
    } else {
        bootContext = cpustate;
        ++bootTicks;
    }
    for (int i = 0; i < numTasks; ++i) {
        if (tasks[i]->state == TaskSleeping
            && (int32_t)(ticks - tasks[i]->wakeTick) >= 0)
            tasks[i]->state = TaskReady;
    }
    // Visit each task and the always-runnable boot context in cyclic order.
    int slot = currentTask < 0 ? numTasks : currentTask;
    for (int visited = 0; visited <= numTasks; ++visited) {
        if (++slot > numTasks) slot = 0;
        if (slot == numTasks) {
            currentTask = -1;
            if (previous != currentTask) ++switches;
            return bootContext;
        }
        Task* task = tasks[slot];
        if (task->state == TaskReady && task->affinityMask == 1) {
            currentTask = slot;
            task->state = TaskRunning;
            ++task->statistics.dispatches;
            if (previous != currentTask) ++switches;
            return task->cpustate;
        }
    }
    return bootContext; // The boot slot above guarantees this is unreachable.
}
uint32_t TaskManager::Ticks() const { return ticks; }
uint32_t TaskManager::BootTicks() const { return bootTicks; }
uint32_t TaskManager::ContextSwitches() const { return switches; }
int TaskManager::TaskCount() const { return numTasks; }
Task* TaskManager::CurrentTask() const { return currentTask < 0 ? 0 : tasks[currentTask]; }
uint32_t TaskManager::OnlineProcessors() const { return 1; }

bool TaskManager::RunSelfTests(GlobalDescriptorTable* gdt) {
    TaskManager manager;
    Task first(gdt, SelfTestEntry), second(gdt, SelfTestEntry);
    CPUState boot = {}, firstFrame = {}, secondFrame = {};
    if (manager.Schedule(&boot) != &boot || manager.Ticks() != 1) return false;
    if (!manager.AddTask(&first) || !manager.AddTask(&second)
        || manager.AddTask(&first) || manager.AddTask(0)) return false;
    if (manager.SetAffinity(&first, 2) || manager.SetAffinity(&first, 3)
        || !manager.SetAffinity(&first, 1)) return false;
    if (manager.Schedule(&boot) != first.cpustate || first.State() != TaskRunning) return false;
    if (manager.Schedule(&firstFrame) != second.cpustate) return false;
    if (manager.Schedule(&secondFrame) != &boot) return false;
    if (!manager.SleepTask(&first, 3) || manager.SleepTask(&first, 1)) return false;
    if (manager.Schedule(&boot) != &secondFrame) return false;
    if (manager.Schedule(&secondFrame) != &boot) return false;
    if (manager.Schedule(&boot) != &firstFrame) return false;
    if (manager.RemoveTask(&first)) return false;
    if (!manager.TerminateTask(&first)) return false;
    if (manager.Schedule(&firstFrame) != &secondFrame) return false;
    if (!manager.RemoveTask(&first) || manager.TaskCount() != 1) return false;
    if (!manager.TerminateTask(&second) || manager.Schedule(&secondFrame) != &boot) return false;
    if (!manager.RemoveTask(&second) || manager.Schedule(&boot) != &boot) return false;
    if (manager.Ticks() != 10 || first.Statistics().runTicks != 2) return false;
    return manager.OnlineProcessors() == 1;
}
