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

// holy chat!
bool MachineHasMemory(MachineId_t machine_id, TaskId_t task_id) {
    MachineInfo_t current_machine = Machine_GetInfo(machine_id);
    unsigned free_memory  = current_machine.memory_size - current_machine.memory_used;
    unsigned memory_needed    = GetTaskMemory(task_id) + 8; // 8MB VM overhead (seems to be large average)
    return (free_memory >= memory_needed);
}

// The Scheduler!

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

    // dont place a task that is already placed or pending
    if(task_placed[task_id]) return;

    VMType_t   required_vm  = RequiredVMType(task_id);
    CPUType_t  required_cpu = RequiredCPUType(task_id);
    SLAType_t  required_sla = RequiredSLA(task_id);
    Priority_t priority     = SLAToPriority(required_sla);
    unsigned   total        = Machine_GetTotal();

    // Round Robin
    if(CURRENT_ALGO == ROUND_ROBIN) {
        for(unsigned i = 0; i < total; i++) {// making sure the machine is on, has memory, and the right cpu type!
            unsigned idx = (rr_counter + i) % total;
            MachineId_t machine = MachineId_t(idx);
            MachineInfo_t machine_info  = Machine_GetInfo(machine);
            if(machine_info.s_state != S0 || machine_info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)){
                continue;
            }

            // never exceed the number of cores — this is the hard limit
            if(machine_info.active_tasks >= machine_info.num_cpus) continue;

            VMId_t target_vm = VMId_t(-1); // find a VM for the current machine, if not make new one
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
            // Add the current task to the VM, and update the round robin counter for the next task
            VM_AddTask(target_vm, task_id, priority);
            task_placed[task_id] = true;
            rr_counter = (idx + 1) % total;
            return;
        } //else, queue the task for later when memory frees up
        SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                  + to_string(task_id), 1);
        pending_tasks.push_back(task_id);
    }

    // Greedy Algorithm
    if(CURRENT_ALGO == GREEDY) {
        MachineId_t best_machine = MachineId_t(-1);
        unsigned best_score = 0;
        bool task_wants_gpu = IsTaskGPUCapable(task_id);

        for(unsigned i = 0; i < total; i++) { //loop through all the machines, find machines for tasks
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);
            if(info.s_state != S0 || info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)) {
                continue;
            }

            // never exceed the number of cores — this is the hard limit
            if(info.active_tasks >= info.num_cpus) continue;

            // we need a way to prefer gpu machines for gpu tasks
            // lets use a scoring system, add a bonus score to the gpu machines
            unsigned score = info.memory_used;
            if(task_wants_gpu && info.gpus) score += 1000000; // GPU bonus score
            if(score >= best_score) {
                best_score   = score;
                best_machine = machine;
            }
        }

        //if we still didnt find a machine, queue it for later when memory frees up
        if(best_machine == MachineId_t(-1)) {
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                    + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        //now that we have machines for tasks, allocate VMs
        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        // if no VMs exist, make one!
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority); // add the task to the VM
        task_placed[task_id] = true;
    }

    // Min-Min Algorithm
    // For each incoming task, estimate how fast each machine can finish it
    // estimated completion time = task instructions / machine MIPS adjusted for contention
    // pick the machine with the minimum estimated completion time
    if(CURRENT_ALGO == MINMIN) {
        MachineId_t best_machine = MachineId_t(-1);
        double best_ect          = 1e18; // start with worst possible time
        bool task_wants_gpu      = IsTaskGPUCapable(task_id);
        unsigned expected_runtime = GetTaskInfo(task_id).target_completion; // in microseconds

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0 || info.cpu != required_cpu
                || !MachineHasMemory(machine, task_id)) {
                continue;
            } // once again, checking all machines if they are alive
            if(info.active_tasks >= info.num_cpus) {
                continue; // just in case, make sure the tasks dont exceed hardware available
            }
            // gpu tasks prefer gpu machines
            if(task_wants_gpu && !info.gpus) continue;

            // MATH TIME!
            // ECT = (1 + load_factor) * expected_runtime / (mips / 1000)
            // load_factor accounts for contention — more tasks = slower per task
            double mips        = (double)info.performance[0]; // P0 MIPS — fastest speed
            double load_factor = (double)info.active_tasks / (double)info.num_cpus;
            double ect         = (1.0 + load_factor) * (double)expected_runtime / (mips / 1000.0);

            // pick the machine with the lowest estimated completion time (cool cool)
            if(ect < best_ect) {
                best_ect     = ect;
                best_machine = machine;
            }
        }

        // if no S0 machine found, try to wake a sleeping one and queue the task
        if(best_machine == MachineId_t(-1)) {
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.cpu != required_cpu) continue;
                if(task_wants_gpu && !info.gpus) continue;
                if(machine_waking[machine]) continue; // already waking, just queue
                if(info.s_state != S0) {
                    Machine_SetState(machine, S0);
                    machine_waking[machine] = true;
                    SimOutput("Scheduler::NewTask(MINMIN): Waking machine "
                              + to_string(machine) + " for task " + to_string(task_id), 2);
                    break;
                }
            }
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                    + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        // repeat of greedy algorithm for vm use
        // now that we have machines for tasks, allocate VMs
        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        // if no VMs exist, make one!
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority); // add the task to the VM
        task_placed[task_id] = true;
    }

    // E-ECO Algorithm
    // consolidate onto fewest machines, throttle cores with P-states,
    // sleep idle machines, wake a machine only as the last resort
    if(CURRENT_ALGO == EECO) {
        bool task_wants_gpu = IsTaskGPUCapable(task_id);

        // find the most-loaded ON machine that still fits
        MachineId_t best_machine = MachineId_t(-1);
        unsigned best_score      = 0;

        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);

            if(info.s_state != S0) continue;
            if(info.cpu != required_cpu) continue;
            if(!MachineHasMemory(machine, task_id)) continue;
            if(info.active_tasks >= info.num_cpus) continue;

            // GPU tasks MUST go to a GPU machine
            if(task_wants_gpu && !info.gpus) continue;

            // Score = active_tasks (prefer busiest so idle machines can actually sleep)
            unsigned score = info.active_tasks + 1;
            if(task_wants_gpu && info.gpus) score += 500; // favor to the gpu
            if(score > best_score) {
                best_score   = score;
                best_machine = machine;
            }
        }

        // if nothing found, try to wake a sleeping machine
        if(best_machine == MachineId_t(-1)) {
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.cpu != required_cpu) continue;
                if(task_wants_gpu && !info.gpus) continue;
                // pick a machine that is off or in a low S-state and not already waking
                if((info.s_state != S0) && !machine_waking[machine]) {
                    Machine_SetState(machine, S0);
                    machine_waking[machine] = true;
                    SimOutput("Scheduler::NewTask(EECO): Waking machine "
                              + to_string(machine) + " for task " + to_string(task_id), 2);
                    // queue the task, StateChangeComplete then PeriodicCheck will place it
                    pending_tasks.push_back(task_id);
                    return;
                }
                // machine is already waking so just queue, it'll be placed when up
                if(machine_waking[machine]) {
                    pending_tasks.push_back(task_id);
                    return;
                }
            }
            // truly no machine of the right type exists, queue and hope lol
            SimOutput("Scheduler::NewTask(EECO): No machine available, queuing task "
                      + to_string(task_id), 1);
            pending_tasks.push_back(task_id);
            return;
        }

        // allocate / reuse a VM on best_machine
        VMId_t target_vm = VMId_t(-1);
        for(VMId_t vm : machine_vms[best_machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(vminfo.vm_type == required_vm && vminfo.cpu == required_cpu) {
                target_vm = vm;
                break;
            }
        }
        // if no VMs exist, make one!
        if(target_vm == VMId_t(-1)) {
            target_vm = VM_Create(required_vm, required_cpu);
            VM_Attach(target_vm, best_machine);
            vms.push_back(target_vm);
            machine_vms[best_machine].push_back(target_vm);
        }
        VM_AddTask(target_vm, task_id, priority); // add the task to the VM
        task_placed[task_id] = true;

        // set all cores on the machine to the right P-state given the current load
        MachineInfo_t info = Machine_GetInfo(best_machine);
        float util = (float)(info.active_tasks + 1) / (float)info.num_cpus;
        CPUPerformance_t pstate;
        if(util > 0.75f)       pstate = P0; // full speed
        else if(util > 0.5f)   pstate = P1;
        else if(util > 0.25f)  pstate = P2;
        else                   pstate = P3; // minimum power
        for(unsigned c = 0; c < info.num_cpus; c++)
            Machine_SetCorePerformance(best_machine, c, pstate);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary

    // only MINMIN and EECO use periodic check for sleep/wake management
    if(CURRENT_ALGO != MINMIN && CURRENT_ALGO != EECO) return;

    unsigned total = Machine_GetTotal();

    // drain the pending queue using the appropriate algorithm
    vector<TaskId_t> still_pending;
    for(TaskId_t pending_id : pending_tasks) {
        if(task_placed[pending_id]) continue;

        VMType_t   req_vm  = RequiredVMType(pending_id);
        CPUType_t  req_cpu = RequiredCPUType(pending_id);
        Priority_t pri     = SLAToPriority(RequiredSLA(pending_id));
        bool wants_gpu     = IsTaskGPUCapable(pending_id);
        unsigned exp_rt    = GetTaskInfo(pending_id).target_completion;

        MachineId_t best = MachineId_t(-1);

        if(CURRENT_ALGO == MINMIN) {
            // use ECT estimation to pick the best machine for pending tasks
            double best_ect = 1e18;
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.s_state != S0 || info.cpu != req_cpu) continue;
                if(!MachineHasMemory(machine, pending_id)) continue;
                if(info.active_tasks >= info.num_cpus) continue;
                if(wants_gpu && !info.gpus) continue;
                double mips        = (double)info.performance[0];
                double load_factor = (double)info.active_tasks / (double)info.num_cpus;
                double ect         = (1.0 + load_factor) * (double)exp_rt / (mips / 1000.0);
                if(ect < best_ect) { best_ect = ect; best = machine; }
            }
        } else { // EECO — consolidation based drain
            unsigned best_score = 0;
            for(unsigned i = 0; i < total; i++) {
                MachineId_t machine = MachineId_t(i);
                MachineInfo_t info  = Machine_GetInfo(machine);
                if(info.s_state != S0 || info.cpu != req_cpu) continue;
                if(!MachineHasMemory(machine, pending_id)) continue;
                if(info.active_tasks >= info.num_cpus) continue;
                if(wants_gpu && !info.gpus) continue;
                unsigned score = info.active_tasks + 1;
                if(score > best_score) { best_score = score; best = machine; }
            }
        }

        if(best == MachineId_t(-1)) {
            still_pending.push_back(pending_id); // still no room, keep waiting
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
        // if no VMs exist, make one!
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

    // sleep idle machines to save energy (both MINMIN and EECO)
    for(unsigned i = 0; i < total; i++) {
        MachineId_t machine = MachineId_t(i);
        MachineInfo_t info  = Machine_GetInfo(machine);
        if(info.s_state != S0 || machine_waking[machine]) continue;
        if(info.active_tasks > 0) continue;
        // check if any VM on this machine still has tasks
        bool has_work = false;
        for(VMId_t vm : machine_vms[machine]) {
            VMInfo_t vminfo = VM_GetInfo(vm);
            if(!vminfo.active_tasks.empty()) { has_work = true; break; }
        }
        if(!has_work) {
            SimOutput("Scheduler::PeriodicCheck(): Sleeping idle machine " + to_string(machine), 2);
            Machine_SetState(machine, S5);
        }
    }

    // EECO P-state tuning — adjust core speeds based on current utilization
    if(CURRENT_ALGO == EECO) {
        for(unsigned i = 0; i < total; i++) {
            MachineId_t machine = MachineId_t(i);
            MachineInfo_t info  = Machine_GetInfo(machine);
            if(info.s_state != S0 || machine_waking[machine] || info.active_tasks == 0) continue;
            float util = (float)info.active_tasks / (float)info.num_cpus;
            CPUPerformance_t pstate = (util > 0.75f) ? P0 :
                                      (util > 0.5f)  ? P1 :
                                      (util > 0.25f) ? P2 : P3;
            for(unsigned c = 0; c < info.num_cpus; c++)
                Machine_SetCorePerformance(machine, c, pstate);
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
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id)
              + " is complete at " + to_string(now), 4);

    // sort pending queue by SLA urgency before draining
    // this ensures SLA0 tasks get placed before SLA1, SLA2, SLA3
    sort(pending_tasks.begin(), pending_tasks.end(), [](TaskId_t a, TaskId_t b) {
        return RequiredSLA(a) < RequiredSLA(b); // SLA0=0 is most urgent
    });

    // Try to place any pending tasks now that a core has freed up
    vector<TaskId_t> still_pending;
    for(TaskId_t pending_id : pending_tasks) {
        // skip if already placed somehow
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

            // never exceed the number of cores — this is the hard limit
            if(info.active_tasks >= info.num_cpus) continue;

            unsigned score = info.memory_used;
            if(wants_gpu && info.gpus) score += 1000000; // GPU bonus score
            if(score >= best_score) {
                best_score   = score;
                best_machine = machine;
            }
        }

        if(best_machine == MachineId_t(-1)) {
            still_pending.push_back(pending_id); // still no room, keep waiting
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
        // if no VMs exist, make one!
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
    pending_tasks = still_pending; // update the queue with tasks still waiting
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
    // boost priority of tasks close to their deadline so they get CPU time faster
    SetTaskPriority(task_id, HIGH_PRIORITY);

    // for MINMIN and EECO, also crank the machine running this task to P0
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
                    // crank all cores to P0 so this task finishes faster
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
    // Called in response to an earlier request to change the state of a machine
    MachineInfo_t info = Machine_GetInfo(machine_id);
    if(info.s_state == S0) {
        machine_waking[machine_id] = false;
    }
}