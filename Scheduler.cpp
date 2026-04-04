//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "Scheduler.hpp"

// 4 types of algorithms, choose here
typedef enum { ROUND_ROBIN, GREEDY, MINMIN, EECO } AlgorithmType;
static AlgorithmType CURRENT_ALGO = EECO;

// shared state for all algorithms
static unsigned rr_counter = 0;
static bool migrating = false;
static std::unordered_map<VMId_t, bool> vm_migrating;       // Track which VMs are currently migrating
static std::unordered_map<MachineId_t, vector<VMId_t>> machine_vms; // list of VMs on machines
static std::unordered_map<MachineId_t, bool> machine_waking; // machines waking up
static vector<TaskId_t> pending_tasks; // tasks waiting for a machine when memory is full
static std::unordered_map<TaskId_t, bool> task_placed; // track if task has been placed already

// Helper Methods!

Priority_t SLAToPriority(SLAType_t sla) {
    switch(sla) {
        case SLA0: return HIGH_PRIORITY;
        case SLA1: return MID_PRIORITY;
        case SLA2: return MID_PRIORITY;
        case SLA3: return LOW_PRIORITY;
        default:   return MID_PRIORITY;
    }
}

bool MachineHasMemory(MachineId_t machine_id, TaskId_t task_id) {
    MachineInfo_t current_machine = Machine_GetInfo(machine_id);
    unsigned free_memory  = current_machine.memory_size - current_machine.memory_used;
    
    VMType_t required_vm = RequiredVMType(task_id);
    CPUType_t required_cpu = RequiredCPUType(task_id);
    bool need_new_vm = true;
    
    for(VMId_t vm : machine_vms[machine_id]) {
        VMInfo_t vminfo = VM_GetInfo(vm);
        if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
            need_new_vm = false;
            break;
        }
    }
    
    unsigned task_memory = GetTaskMemory(task_id);
    unsigned vm_overhead = need_new_vm ? 8 : 0;
    unsigned total_needed = task_memory + vm_overhead;
    
    return (free_memory >= total_needed);
}

double ComputeMinMinECT(MachineInfo_t info, TaskId_t task_id) {
    unsigned expected_runtime = GetTaskInfo(task_id).target_completion;
    double mips = (double)info.performance[0];
    double load_factor = (double)info.active_tasks / (double)info.num_cpus;
    double ect = (1.0 + load_factor) * (double)expected_runtime / (mips / 1000.0);

    SLAType_t sla = RequiredSLA(task_id);
    if(sla == SLA0)      ect *= 0.60;
    else if(sla == SLA1) ect *= 0.80;

    return ect;
}

void WakeMinMinMachines(CPUType_t req_cpu, bool wants_gpu) {
    unsigned total = Machine_GetTotal();
    unsigned pending_count = pending_tasks.size();

    unsigned wake_budget = 1;
    if(pending_count >= 1500) wake_budget = 4;
    else if(pending_count >= 750) wake_budget = 3;
    else if(pending_count >= 250) wake_budget = 2;

    for(unsigned i = 0; i < total && wake_budget > 0; i++) {
        MachineId_t machine = MachineId_t(i);
        MachineInfo_t info  = Machine_GetInfo(machine);

        if(info.cpu != req_cpu) continue;
        if(wants_gpu && !info.gpus) continue;
        if(machine_waking[machine]) continue;
        if(info.s_state == S0) continue;

        Machine_SetState(machine, S0);
        machine_waking[machine] = true;
        wake_budget--;

        SimOutput("Scheduler::MINMIN: Waking machine " + to_string(machine), 2);
    }
}

int ComputeEECOScore(MachineInfo_t info, bool task_wants_gpu) {
    float util = (float)info.active_tasks / (float)info.num_cpus;
    int score = (int)(util * 1000.0f);

    if(info.active_tasks > 0) score += 500;
    if(info.active_vms > 0) score += 100;
    if(task_wants_gpu && info.gpus) score += 500;

    return score;
}

// testing
static Time_t last_print_time = 0;
static const Time_t PRINT_INTERVAL = 10000000; // 10 seconds sim time

void PrintStatus(Time_t now) {
    if(now - last_print_time < PRINT_INTERVAL) return;
    last_print_time = now;

    cout << "\n[STATUS] time=" << (now/1000000)
         << "s | pending=" << pending_tasks.size()
         << " | placed=" << task_placed.size()
         << endl;
    cout.flush();
}

// The Scheduler!

void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    for(unsigned i = 0; i < Machine_GetTotal(); i++) {
        machines.push_back(MachineId_t(i));
        machine_vms[MachineId_t(i)] = vector<VMId_t>();
        machine_waking[MachineId_t(i)] = false;
    }

    SimOutput("Scheduler::Init(): Done", 1);
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    PrintStatus(now);

    if(task_placed[task_id]) return;

    VMType_t   required_vm  = RequiredVMType(task_id);
    CPUType_t  required_cpu = RequiredCPUType(task_id);
    SLAType_t  required_sla = RequiredSLA(task_id);
    Priority_t priority     = SLAToPriority(required_sla);
    unsigned   total        = Machine_GetTotal();

    // Round Robin
    if(CURRENT_ALGO == ROUND_ROBIN) {
        for(unsigned i = 0; i < total; i++) {
            unsigned idx = (rr_counter + i) % total;
            MachineId_t machine = MachineId_t(idx);
            MachineInfo_t machine_info  = Machine_GetInfo(machine);
            if(machine_info.s_state != S0 || machine_info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)){
                continue;
            }

            if(machine_info.active_tasks >= machine_info.num_cpus) continue;

            VMId_t target_vm = VMId_t(-1);
            for(VMId_t vm : machine_vms[machine]) {
                VMInfo_t vminfo = VM_GetInfo(vm);
                if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                    target_vm = vm;
                    break;
                }
            }
            if(target_vm == VMId_t(-1)) {
                target_vm = VM_Create(required_vm, required_cpu);
                VM_Attach(target_vm, machine);
                vms.push_back(target_vm);
                machine_vms[machine].push_back(target_vm);
            }
            VM_AddTask(target_vm, task_id, priority);
            task_placed[task_id] = true;
            rr_counter = (idx + 1) % total;
            return;
        }
        SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                  + to_string(task_id), 1);
        pending_tasks.push_back(task_id);
        return;
    }

    // Greedy Algorithm
    if(CURRENT_ALGO == GREEDY) {
        MachineId_t best_machine = MachineId_t(-1);
        unsigned best_score = 0;
        unsigned best_perf = 0;
        bool task_wants_gpu = IsTaskGPUCapable(task_id);

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);
            if(info.s_state != S0 || info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)) {
                continue;
            }

            if(info.active_tasks >= info.num_cpus) continue;

            unsigned score = info.memory_used;
            if(task_wants_gpu && info.gpus) score += 1000000;

            unsigned perf = info.performance[0];

            if(best_machine == MachineId_t(-1) ||
               score > best_score ||
               (score == best_score && perf > best_perf)) {
                best_score = score;
                best_perf = perf;
                best_machine = machine;
            }
        }

        if(best_machine == MachineId_t(-1)) {
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                    + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority);
        task_placed[task_id] = true;
        return;
    }

    // Min-Min Algorithm
    if(CURRENT_ALGO == MINMIN) {
        MachineId_t best_machine = MachineId_t(-1);
        double best_ect          = 1e18;
        bool task_wants_gpu      = IsTaskGPUCapable(task_id);

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0 || info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)) {
                continue;
            }
            if(info.active_tasks >= info.num_cpus) continue;
            if(task_wants_gpu && !info.gpus) continue;

            double ect = ComputeMinMinECT(info, task_id);

            if(ect < best_ect) {
                best_ect     = ect;
                best_machine = machine;
            }
        }

        if(best_machine == MachineId_t(-1)) {
            WakeMinMinMachines(required_cpu, task_wants_gpu);
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                    + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority);
        task_placed[task_id] = true;
        return;
    }

    // E-ECO Algorithm
    if(CURRENT_ALGO == EECO) {
        bool task_wants_gpu = IsTaskGPUCapable(task_id);

        MachineId_t best_machine = MachineId_t(-1);
        int best_score = -1;

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0) continue;
            if(info.cpu != required_cpu) continue;
            if(!MachineHasMemory(machine, task_id)) continue;
            if(info.active_tasks >= info.num_cpus) continue;
            if(task_wants_gpu && !info.gpus) continue;

            int score = ComputeEECOScore(info, task_wants_gpu);

            if(score > best_score) {
                best_score   = score;
                best_machine = machine;
            }
        }

        if(best_machine == MachineId_t(-1)) {
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.cpu != required_cpu) continue;
                if(task_wants_gpu && !info.gpus) continue;
                if(info.s_state != S0 && !machine_waking[machine]) {
                    Machine_SetState(machine, S0);
                    machine_waking[machine] = true;
                    SimOutput("Scheduler::NewTask(EECO): Waking machine "
                              + to_string(machine) + " for task " + to_string(task_id), 2);
                    pending_tasks.push_back(task_id);
                    return;
                }
                if(machine_waking[machine]) {
                    pending_tasks.push_back(task_id);
                    return;
                }
            }
            SimOutput("Scheduler::NewTask(EECO): No machine available, queuing task "
                      + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority);
        task_placed[task_id] = true;

        MachineInfo_t info = Machine_GetInfo(best_machine);
        float util = (float)(info.active_tasks + 1) / (float)info.num_cpus;
        CPUPerformance_t pstate;
        if(util > 0.90f)       pstate = P0;
        else if(util > 0.65f)  pstate = P1;
        else if(util > 0.35f)  pstate = P2;
        else                   pstate = P3;
        for(unsigned c = 0; c < info.num_cpus; c++)
            Machine_SetCorePerformance(best_machine, c, pstate);
        return;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    PrintStatus(now);

    if(CURRENT_ALGO == ROUND_ROBIN) {
        bool made_progress = true;

        while(made_progress && !pending_tasks.empty()) {
            made_progress = false;
            vector<TaskId_t> still_pending;

            for(TaskId_t pending_id : pending_tasks) {
                if(task_placed[pending_id]) continue;

                VMType_t   req_vm  = RequiredVMType(pending_id);
                CPUType_t  req_cpu = RequiredCPUType(pending_id);
                Priority_t pri     = SLAToPriority(RequiredSLA(pending_id));
                unsigned   total   = Machine_GetTotal();

                bool placed = false;
                for(unsigned i = 0; i < total && !placed; i++) {
                    unsigned idx = (rr_counter + i) % total;
                    MachineId_t machine = MachineId_t(idx);
                    MachineInfo_t info = Machine_GetInfo(machine);
                    
                    if(info.s_state != S0 || info.cpu != req_cpu
                        || !MachineHasMemory(machine, pending_id)) {
                        continue;
                    }
                    if(info.active_tasks >= info.num_cpus) continue;

                    VMId_t target_vm = VMId_t(-1);
                    for(VMId_t vm : machine_vms[machine]) {
                        VMInfo_t vminfo = VM_GetInfo(vm);
                        if(vminfo.vm_type == req_vm && vminfo.cpu == req_cpu) {
                            target_vm = vm;
                            break;
                        }
                    }
                    
                    if(target_vm == VMId_t(-1)) {
                        target_vm = VM_Create(req_vm, req_cpu);
                        VM_Attach(target_vm, machine);
                        vms.push_back(target_vm);
                        machine_vms[machine].push_back(target_vm);
                    }
                    
                    VM_AddTask(target_vm, pending_id, pri);
                    task_placed[pending_id] = true;
                    rr_counter = (idx + 1) % total;
                    placed = true;
                    made_progress = true;
                    SimOutput("Scheduler::PeriodicCheck(RR): Placed pending task " 
                              + to_string(pending_id), 2);
                }
                
                if(!placed) {
                    still_pending.push_back(pending_id);
                }
            }

            pending_tasks = still_pending;
        }
        return;
    }

    if(CURRENT_ALGO != MINMIN && CURRENT_ALGO != EECO) return;

    unsigned total = Machine_GetTotal();

    if(CURRENT_ALGO == MINMIN) {
        sort(pending_tasks.begin(), pending_tasks.end(), [](TaskId_t a, TaskId_t b) {
            return RequiredSLA(a) < RequiredSLA(b);
        });
    }

    vector<TaskId_t> still_pending;
    for(TaskId_t pending_id : pending_tasks) {
        if(task_placed[pending_id]) continue;

        VMType_t   req_vm  = RequiredVMType(pending_id);
        CPUType_t  req_cpu = RequiredCPUType(pending_id);
        Priority_t pri     = SLAToPriority(RequiredSLA(pending_id));
        bool wants_gpu     = IsTaskGPUCapable(pending_id);

        MachineId_t best = MachineId_t(-1);

        if(CURRENT_ALGO == MINMIN) {
            double best_ect = 1e18;
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.s_state != S0 || info.cpu != req_cpu) continue;
                if(!MachineHasMemory(machine, pending_id)) continue;
                if(info.active_tasks >= info.num_cpus) continue;
                if(wants_gpu && !info.gpus) continue;

                double ect = ComputeMinMinECT(info, pending_id);
                if(ect < best_ect) { best_ect = ect; best = machine; }
            }
        } else {
            int best_score = -1;
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.s_state != S0) continue;
                if(info.cpu != req_cpu) continue;
                if(!MachineHasMemory(machine, pending_id)) continue;
                if(info.active_tasks >= info.num_cpus) continue;
                if(wants_gpu && !info.gpus) continue;

                int score = ComputeEECOScore(info, wants_gpu);

                if(score > best_score) { best_score = score; best = machine; }
            }
        }

        if(best == MachineId_t(-1)) {
            if(CURRENT_ALGO == MINMIN) {
                WakeMinMinMachines(req_cpu, wants_gpu);
            }

            if(CURRENT_ALGO == EECO) {
                for(unsigned i = 0; i < total; i++) {
                    MachineId_t machine = MachineId_t(i);
                    MachineInfo_t info  = Machine_GetInfo(machine);
                    if(info.cpu != req_cpu) continue;
                    if(wants_gpu && !info.gpus) continue;
                    if(info.s_state != S0 && !machine_waking[machine]) {
                        Machine_SetState(machine, S0);
                        machine_waking[machine] = true;
                        SimOutput("Scheduler::PeriodicCheck(EECO): Waking machine "
                                  + to_string(machine) + " for pending task "
                                  + to_string(pending_id), 2);
                        break;
                    }
                }
            }

            still_pending.push_back(pending_id);
            continue;
        }

        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == req_vm && vminfo.cpu == req_cpu) {
                target_vm = vm;
                break;
            }
        }
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(req_vm, req_cpu);
            VM_Attach(target_vm, best);
            vms.push_back(target_vm);
            machine_vms[best].push_back(target_vm);
        }
        VM_AddTask(target_vm, pending_id, pri);
        task_placed[pending_id] = true;
        SimOutput("Scheduler::PeriodicCheck(): Placed pending task " + to_string(pending_id), 2);
    }
    pending_tasks = still_pending;

    // For EECO, keep P-state tuning but do NOT sleep machines here.
    if(CURRENT_ALGO == EECO) {
        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);
            if(info.s_state != S0 || machine_waking[machine] || info.active_tasks == 0) continue;
            float util = (float)info.active_tasks / (float)info.num_cpus;
            CPUPerformance_t pstate = (util > 0.90f) ? P0 :
                                      (util > 0.65f) ? P1 :
                                      (util > 0.35f) ? P2 : P3;
            for(unsigned c = 0; c < info.num_cpus; c++)
                Machine_SetCorePerformance(machine, c, pstate);
        }
    }
}

void Scheduler::Shutdown(Time_t time) {
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    PrintStatus(now);

    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id)
              + " is complete at " + to_string(now), 4);

    if(CURRENT_ALGO == MINMIN || CURRENT_ALGO == EECO) {
        return;
    }

    sort(pending_tasks.begin(), pending_tasks.end(), [](TaskId_t a, TaskId_t b) {
        return RequiredSLA(a) < RequiredSLA(b);
    });

    vector<TaskId_t> still_pending;
    for(TaskId_t pending_id : pending_tasks) {
        if(task_placed[pending_id]) continue;

        VMType_t  req_vm  = RequiredVMType(pending_id);
        CPUType_t req_cpu = RequiredCPUType(pending_id);
        Priority_t pri    = SLAToPriority(RequiredSLA(pending_id));
        bool wants_gpu    = IsTaskGPUCapable(pending_id);
        unsigned total    = Machine_GetTotal();

        MachineId_t best_machine = MachineId_t(-1);
        unsigned best_score = 0;

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);
            if(info.s_state != S0 || info.cpu != req_cpu
                || !MachineHasMemory(machine, pending_id)) continue;

            if(info.active_tasks >= info.num_cpus) continue;

            unsigned score = info.memory_used;
            if(wants_gpu && info.gpus) score += 1000000;
            if(score >= best_score) {
                best_score   = score;
                best_machine = machine;
            }
        }

        if(best_machine == MachineId_t(-1)) {
            still_pending.push_back(pending_id);
            continue;
        }

        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == req_vm && vminfo.cpu == req_cpu) {
                target_vm = vm;
                break;
            }
        }
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(req_vm, req_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, pending_id, pri);
        task_placed[pending_id] = true;
        SimOutput("Scheduler::TaskComplete(): Placed pending task "
                  + to_string(pending_id), 1);
    }
    pending_tasks = still_pending;
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    vm_migrating[vm_id] = false;
    migrating = false;
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id)
              + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id)
              + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id)
              + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id)
              + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    SetTaskPriority(task_id, HIGH_PRIORITY);

    if(CURRENT_ALGO != MINMIN && CURRENT_ALGO != EECO) return;
    unsigned total = Machine_GetTotal();
    for(unsigned i = 0; i < total; i++) {
        MachineId_t machine = MachineId_t(i);
        MachineInfo_t minfo = Machine_GetInfo(machine);
        if(minfo.s_state != S0) continue;
        for(VMId_t vm : machine_vms[machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            for(TaskId_t t : vminfo.active_tasks) {
                if(t == task_id) {
                    for(unsigned c = 0; c < minfo.num_cpus; c++)
                        Machine_SetCorePerformance(machine, c, P0);
                    SimOutput("SLAWarning(): Boosted machine " + to_string(machine)
                              + " to P0 for task " + to_string(task_id), 2);
                    return;
                }
            }
        }
    }
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if(info.s_state == S0) {
        machine_waking[machine_id] = false;
    }
}