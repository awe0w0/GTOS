#include <multitasking.h>

using namespace gtos;

void printf(char*);

void taskA();

Task::Task(GlobalDescriptorTable* gdt, void entrypoint()) {
    cpustate = (CPUState*)(stack + 4096 - sizeof(CPUState));
    cpustate->eax = 0;
    cpustate->ebx = 0;
    cpustate->ecx = 0;
    cpustate->edx = 0;

    cpustate->esi = 0;
    cpustate->edi = 0;
    cpustate->ebp = 0;

    /*
    cpustate->gs = 0;
    cpustate->fs = 0;
    cpustate->ds = 0;
    cpustate->es = 0;
    */

    //cpustate->error = 0;

    cpustate->eip = (uint32_t)entrypoint;
    cpustate->cs = gdt->CodeSegmentSelector(); //不能为0x8
    cpustate->eflags = 0x202;
}

Task::~Task() {

}

TaskManager::TaskManager() {
    numTasks = 0;
    currentTask = -1;
}

TaskManager::~TaskManager() {

}

bool TaskManager::AddTask(Task* task) {
    if (numTasks >= 256) return false;
    tasks[numTasks++] = task;
    return true;
}



//调用这个函数获取下个进程的esp
CPUState* TaskManager::Schedule(CPUState* cpustate) {
    if (numTasks <= 0) return cpustate;
    //存储当前值
    if (currentTask >= 0) tasks[currentTask]->cpustate = cpustate;

    //当前任务超过数组大小，重新开始
    if (++currentTask >= numTasks) {
        currentTask %= numTasks;
    }
    
    // 让cpu直接弹出当前任务的栈
    return  tasks[currentTask]->cpustate;
}