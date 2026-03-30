//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include <cstdint>
#include <unordered_map>
#include "Scheduler.hpp"
#include <deque>

// 4 types of algorithms, choose here
typedef enum { ROUND_ROBIN, GREEDY, PMAPPER, EECO } AlgorithmType;
static AlgorithmType CURRENT_ALGO = GREEDY;

// ─── Shared state ─────────────────────────────────────────────────────
static unsigned rr_counter = 0;
static bool migrating = false;
static std::unordered_map<VMId_t, bool> vm_migrating;       // Track which VMs are currently migrating
static std::unordered_map<MachineId_t, vector<VMId_t>> machine_vms; // list of VMs on machines
static std::unordered_map<MachineId_t, bool> machine_waking; // machines waking up
static std::deque<TaskId_t> pending_tasks;
static std::unordered_map<MachineId_t, Time_t> machine_idle_since;

// Helper Methods!

VMId_t FindReusableVM(MachineId_t machine, TaskId_t task_id) {
    VMType_t required_vm   = RequiredVMType(task_id);
    CPUType_t required_cpu = RequiredCPUType(task_id);

    for(VMId_t vm : machine_vms[machine]) {
        if(vm_migrating[vm]) continue;
        VMInfo_t vminfo = VM_GetInfo(vm);
        if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
            return vm;
        }
    }
    return VMId_t(-1);
}

bool TryPlaceTaskNow_PMapper(TaskId_t task_id) {
    VMType_t   required_vm  = RequiredVMType(task_id);
    CPUType_t  required_cpu = RequiredCPUType(task_id);
    SLAType_t  required_sla = RequiredSLA(task_id);
    Priority_t priority     = SLAToPriority(required_sla);

    MachineId_t best_machine = MachineId_t(-1);
    int best_score = -1;

    // Prefer already-awake machines, and pack onto the most loaded one
    // that still has room. This is the core pMapper idea.
    for(unsigned i = 0; i < Machine_GetTotal(); i++) {
        MachineId_t machine = MachineId_t(i);
        MachineInfo_t info = Machine_GetInfo(machine);

        if(info.s_state != S0) continue;
        if(info.cpu != required_cpu) continue;
        if(!MachineHasMemory(machine, task_id)) continue;
        if(info.active_tasks >= info.num_cpus * 2) continue;

        int score = (int)info.active_tasks;
        if(score > best_score) {
            best_score = score;
            best_machine = machine;
        }
    }

    if(best_machine != MachineId_t(-1)) {
        VMId_t vm = FindReusableVM(best_machine, task_id);
        if(vm == VMId_t(-1)) {
            vm = VM_Create(required_vm, required_cpu);
            VM_Attach(vm, best_machine);
            vms.push_back(vm);
            machine_vms[best_machine].push_back(vm);
            vm_migrating[vm] = false;
        }
        VM_AddTask(vm, task_id, priority);
        machine_idle_since[best_machine] = 0;
        return true;
    }

    // If no awake machine works, wake one matching sleeping machine
    for(unsigned i = 0; i < Machine_GetTotal(); i++) {
        MachineId_t machine = MachineId_t(i);
        MachineInfo_t info = Machine_GetInfo(machine);

        if(info.cpu != required_cpu) continue;
        if(info.s_state == S0) continue;
        if(machine_waking[machine]) continue;

        Machine_SetState(machine, S0);
        machine_waking[machine] = true;
        pending_tasks.push_back(task_id);
        return true;
    }

    return false;
}

void RetryPendingTasks_PMapper() {
    size_t count = pending_tasks.size();
    for(size_t i = 0; i < count; i++) {
        TaskId_t task = pending_tasks.front();
        pending_tasks.pop_front();

        if(IsTaskCompleted(task)) continue;

        if(!TryPlaceTaskNow_PMapper(task)) {
            pending_tasks.push_back(task);
        }
    }
}

// priority helper method to clear up the code a bit
Priority_t SLAToPriority(SLAType_t sla) {
    switch(sla) {
        case SLA0: return HIGH_PRIORITY;
        case SLA1: return MID_PRIORITY;
        case SLA2: return MID_PRIORITY;
        case SLA3: return LOW_PRIORITY;
        default:   return MID_PRIORITY;
    }
}

// holy chat
bool MachineHasMemory(MachineId_t machine_id, TaskId_t task_id) {
    MachineInfo_t info = Machine_GetInfo(machine_id);
    unsigned free_mem  = info.memory_size - info.memory_used;
    unsigned needed    = GetTaskMemory(task_id);
    return free_mem >= needed + 8; // 8MB VM overhead
}

// ─── Scheduler methods ────────────────────────────────────────────────

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
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
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM,
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    // Algorithm logic is implemented here!

    VMType_t   required_vm  = RequiredVMType(task_id);
    CPUType_t  required_cpu = RequiredCPUType(task_id);
    SLAType_t  required_sla = RequiredSLA(task_id);
    Priority_t priority     = SLAToPriority(required_sla);
    unsigned   total        = Machine_GetTotal();

    // Round Robin
    if(CURRENT_ALGO == ROUND_ROBIN) {
        for(unsigned i = 0; i < total; i++) {
            unsigned idx        = (rr_counter + i) % total;
            MachineId_t machine = MachineId_t(idx);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0) continue;
            if(info.cpu != required_cpu) continue;
            if(!MachineHasMemory(machine, task_id)) continue;

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
            rr_counter = (idx + 1) % total;
            return;
        }
        SimOutput("Scheduler::NewTask(): No suitable machine found for task "
                  + to_string(task_id), 1);
    }

    // Greedy Algorithm
    if(CURRENT_ALGO == GREEDY) {
        MachineId_t best_machine = MachineId_t(-1);
        unsigned best_load = 0;

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0) continue;
            if(info.cpu != required_cpu) continue;
            if(!MachineHasMemory(machine, task_id)) continue;

            // Pick most loaded machine that still has headroom
            if(info.memory_used >= best_load &&
               info.active_tasks < info.num_cpus * 2) {
                best_load    = info.memory_used;
                best_machine = machine;
            }
        }
        if(best_machine == MachineId_t(-1)) {
            SimOutput("Scheduler::NewTask(): No suitable machine found for task "
                      + to_string(task_id), 1);
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
    }

    // pMapper
    if(CURRENT_ALGO == PMAPPER) {
        if(!TryPlaceTaskNow_PMapper(task_id)) {
            SimOutput("pMapper: failed to place task " + to_string(task_id), 1);
        }
        return;
    }

    // eeco
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    // Sleep logic will be implemented in pMapper

    if(CURRENT_ALGO != PMAPPER) return;

    RetryPendingTasks_PMapper();

    for(unsigned i = 0; i < Machine_GetTotal(); i++) {
        MachineId_t m = MachineId_t(i);
        MachineInfo_t info = Machine_GetInfo(m);

        if(info.s_state != S0) continue;

        if(info.active_tasks == 0) {
            if(machine_idle_since[m] == 0) {
                machine_idle_since[m] = now;
            }

            // Sleep idle machines after a little while
            if(now - machine_idle_since[m] > 200000) {
                Machine_SetState(m, S3);
            }
        } else {
            machine_idle_since[m] = 0;
        }
    }
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    // Sleep logic is handled in PeriodicCheck

    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id)
              + " is complete at " + to_string(now), 4);

    if(CURRENT_ALGO == PMAPPER) {
        RetryPendingTasks_PMapper();
    }

}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
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
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id)
              + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id)
              + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    // SLA response will be implemented in pMapper
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if(info.s_state == S0) {
        machine_waking[machine_id] = false;
        if(CURRENT_ALGO == PMAPPER) {
            RetryPendingTasks_PMapper();
        }
    }
}