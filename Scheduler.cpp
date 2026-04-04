//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//
// Project Members: Dean Chaudhry, Lucas Berio Perez

#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "Scheduler.hpp"

// 4 Algorithmns, we choose the one to run here!
typedef enum { ROUND_ROBIN, GREEDY, MINMIN, EECO } AlgorithmType;
static AlgorithmType currentAlgo = MINMIN;

// shared state for all algorithms
static unsigned rrCounter = 0;
static bool migrating = false;
static std::unordered_map<VMId_t, bool> vmMigrating;
static std::unordered_map<MachineId_t, vector<VMId_t>> machineVms;
static std::unordered_map<MachineId_t, bool> machineWaking;
static vector<TaskId_t> pendingTasks;
static std::unordered_map<TaskId_t, bool> taskPlaced;

// HELPER METHODS

// Just a helper for the priority of tasks
Priority_t slaToPriority(SLAType_t sla) {
    switch (sla) {
        case SLA0: return HIGH_PRIORITY;
        case SLA1: return MID_PRIORITY;
        case SLA2: return MID_PRIORITY;
        case SLA3: return LOW_PRIORITY;
        default:   return MID_PRIORITY;
    }
}

// method that checks if a machine has enough memory to run for specfific tasks
bool machineHasMemory(MachineId_t machineId, TaskId_t taskId) {
    MachineInfo_t machineInfo = Machine_GetInfo(machineId);
    unsigned freeMemory = machineInfo.memory_size - machineInfo.memory_used;
    VMType_t neededVm = RequiredVMType(taskId);
    CPUType_t neededCpu = RequiredCPUType(taskId); //getting the needed requirements for the task
    bool needsNewVm = true;
	// then check whcih vms are on the machine and if we can run them
    for (VMId_t vmId : machineVms[machineId]) {
        VMInfo_t vmInfo = VM_GetInfo(vmId);
        if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
            needsNewVm = false;
            break;
        }
    }
    unsigned taskMemory = GetTaskMemory(taskId);
    unsigned vmOverhead = needsNewVm ? 8 : 0; //IMPORTANT!!
    unsigned totalNeeded = taskMemory + vmOverhead; //if we make a new vm, we need to add memory for overhead
    return freeMemory >= totalNeeded;
}

// method that computes teh Energy Efficient Compute Operation score (EECO)
int computeEecoScore(MachineInfo_t machineInfo, bool wantsGpu) {
    float util = (float)machineInfo.active_tasks / (float)machineInfo.num_cpus;
    int score = (int)(util * 1000.0f);
    if (machineInfo.active_tasks > 0) score += 500;
    if (machineInfo.active_vms > 0) score += 100;
    if (wantsGpu && machineInfo.gpus) score += 500;
    return score;
}

// Testing
 // DEBUGGING METHOD! helps to see if we are in a loop or the task is still running
static Time_t lastPrintTime = 0;
static const Time_t printInterval = 10000000; // 10 seconds sim time
void printStatus(Time_t now) {
    if (now - lastPrintTime < printInterval) return;
    lastPrintTime = now;
    cout << "\n[STATUS] time=" << (now / 1000000)
         << "s | pending=" << pendingTasks.size()
         << " | placed=" << taskPlaced.size()
         << endl;
    cout.flush();
}

// The Scheduler! The Big Boss!
void Scheduler::Init() { //initialize scheduler, machine info, vms, etc
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    for (unsigned i = 0; i < Machine_GetTotal(); i++) {
        machines.push_back(MachineId_t(i));
        machineVms[MachineId_t(i)] = vector<VMId_t>();
        machineWaking[MachineId_t(i)] = false;
    }
    SimOutput("Scheduler::Init(): Done", 1);
}

//Scheduler Code Goes here! 4 algorithms :)
void Scheduler::NewTask(Time_t now, TaskId_t taskId) {
    printStatus(now); // used for debugging to see if we are still alive
    if (taskPlaced[taskId]) return;
    VMType_t neededVm = RequiredVMType(taskId);
    CPUType_t neededCpu = RequiredCPUType(taskId);
    SLAType_t neededSla = RequiredSLA(taskId);
    Priority_t priority = slaToPriority(neededSla);
    unsigned totalMachines = Machine_GetTotal(); //variables to be used

    // Round Robin
    if (currentAlgo == ROUND_ROBIN) {
        for (unsigned i = 0; i < totalMachines; i++) {// making sure the machine is on, has memory, and the right cpu type!
            unsigned idx = (rrCounter + i) % totalMachines;
            MachineId_t machineId = MachineId_t(idx);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
			// if the machine is not the one we want, skip it!
            if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu
                || !machineHasMemory(machineId, taskId)) {
                continue;
            }
            if (machineInfo.active_tasks >= machineInfo.num_cpus) continue; //check amchine status

			//now we check if we have a vm to run the current task
            VMId_t targetVm = VMId_t(-1);
            for (VMId_t vmId : machineVms[machineId]) {
                VMInfo_t vmInfo = VM_GetInfo(vmId);
                if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                    targetVm = vmId;
                    break;
                }
            }
            if (targetVm == VMId_t(-1)) { //if there is no vm, we make one!
                targetVm = VM_Create(neededVm, neededCpu);
                VM_Attach(targetVm, machineId);
                vms.push_back(targetVm);
                machineVms[machineId].push_back(targetVm);
            }
            VM_AddTask(targetVm, taskId, priority);
            taskPlaced[taskId] = true;
            rrCounter = (idx + 1) % totalMachines;
            return; //put the task on the machine and we are done!
        }
		//debugging output code ;)
        SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                  + to_string(taskId), 1);
        pendingTasks.push_back(taskId);
        return;
    }

    // Greedy Algorithm
    if (currentAlgo == GREEDY) {
        MachineId_t bestMachine = MachineId_t(-1);
        unsigned bestScore = 0;
        unsigned bestPerf = 0; //have variables that decide which is the best machien
        bool wantsGpu = IsTaskGPUCapable(taskId);
        for (unsigned i = 0; i < totalMachines; i++) {
            MachineId_t machineId = MachineId_t(i);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
			//do the same thing as above, check machines!
            if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu
                || !machineHasMemory(machineId, taskId)) {
                continue;
            }
            if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;

			// We need to compute the machines score based on memory and gpu status
            unsigned score = machineInfo.memory_used;
            if (wantsGpu && machineInfo.gpus) score += 1000000;
            unsigned perf = machineInfo.performance[0];
			//update the best machine
            if (bestMachine == MachineId_t(-1) || score > bestScore || (score == bestScore && perf > bestPerf)) {
                bestScore = score;
                bestPerf = perf;
                bestMachine = machineId;
            }
        }
        if (bestMachine == MachineId_t(-1)) { //wake up machines if there are no machines available
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                      + to_string(taskId), 1);
            pendingTasks.push_back(taskId);
            return;
        }
		//same vm code as above in round robin!
        VMId_t targetVm = VMId_t(-1);
        for (VMId_t vmId : machineVms[bestMachine]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                targetVm = vmId;
                break;
            }
        }
        if (targetVm == VMId_t(-1)) {
            targetVm = VM_Create(neededVm, neededCpu);
            VM_Attach(targetVm, bestMachine);
            vms.push_back(targetVm);
            machineVms[bestMachine].push_back(targetVm);
        }
        VM_AddTask(targetVm, taskId, priority);
        taskPlaced[taskId] = true;
        return;
    }

    // Min-Min Algorithm
    if (currentAlgo == MINMIN) {
        MachineId_t bestMachine = MachineId_t(-1);
        double bestEct = 1e18; //variables to find the best machine to run based on the "estimated" completion time
        bool wantsGpu = IsTaskGPUCapable(taskId);
        unsigned expectedRuntime = GetTaskInfo(taskId).target_completion;
        for (unsigned i = 0; i < totalMachines; i++) {
            MachineId_t machineId = MachineId_t(i);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
            if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu
                || !machineHasMemory(machineId, taskId)) {
                continue;
            }
            if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;
            if (wantsGpu && !machineInfo.gpus) continue;

			// Estimation Calculation! Take in the machine performance, load, estimation time to get completino time
            double mips = (double)machineInfo.performance[0];
            double loadFactor = (double)machineInfo.active_tasks / (double)machineInfo.num_cpus;
            double ect = (1.0 + loadFactor) * (double)expectedRuntime / (mips / 1000.0);
            if (ect < bestEct) { //update best machine
                bestEct = ect;
                bestMachine = machineId;
            }
        }
		// if there is no machine to run, we wake up the machines that are off
        if (bestMachine == MachineId_t(-1)) {
            for (unsigned i = 0; i < totalMachines; i++) {
                MachineId_t machineId = MachineId_t(i);
                MachineInfo_t machineInfo = Machine_GetInfo(machineId);
                if (machineInfo.cpu != neededCpu) continue;
                if (wantsGpu && !machineInfo.gpus) continue;//AHHHHHHHHHHHHHHHHHHHHHH
                if (machineWaking[machineId]) continue; //if we are already waking this machine, skip it
                if (machineInfo.s_state != S0) { //OH MY GOD!
                    Machine_SetState(machineId, S0);
                    machineWaking[machineId] = true;
                    SimOutput("Scheduler::NewTask(MINMIN): Waking machine "
                              + to_string(machineId) + " for task " + to_string(taskId), 2);
                    break;//error check wake only one up at a time
                }
            }
            SimOutput("Scheduler::NewTask(): No suitable machine found, queuing task "
                      + to_string(taskId), 1); //this is cool! we dont waste any time!
            pendingTasks.push_back(taskId); //queue the correct next task or we are cooked!
            return;
        }
		//same vm knowledge
        VMId_t targetVm = VMId_t(-1);
        for (VMId_t vmId : machineVms[bestMachine]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                targetVm = vmId;
                break;
            }
        }
        if (targetVm == VMId_t(-1)) {
            targetVm = VM_Create(neededVm, neededCpu);
            VM_Attach(targetVm, bestMachine);
            vms.push_back(targetVm);
            machineVms[bestMachine].push_back(targetVm);
        }
        VM_AddTask(targetVm, taskId, priority);
        taskPlaced[taskId] = true;
        return;
    }

    // EECO Algorithm
    if (currentAlgo == EECO) {
		// we will compute the Energy efficiency score (EECO) for each machine, then distribute taskss
        bool wantsGpu = IsTaskGPUCapable(taskId); 
        MachineId_t bestMachine = MachineId_t(-1);
        int bestScore = -1;
		//compute the score with helper!
        for (unsigned i = 0; i < totalMachines; i++) {
            MachineId_t machineId = MachineId_t(i);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
            if (machineInfo.s_state != S0) continue;
            if (machineInfo.cpu != neededCpu) continue;
            if (!machineHasMemory(machineId, taskId)) continue;
            if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;
            if (wantsGpu && !machineInfo.gpus) continue;
            int score = computeEecoScore(machineInfo, wantsGpu);
			//get the best machine with the best energy efficiency score :)
            if (score > bestScore) {
                bestScore = score;
                bestMachine = machineId;
            }
        }
		// same logic for waking up machines as minmin, but take the most energy efficient machine to wake up
        if (bestMachine == MachineId_t(-1)) {
            for (unsigned i = 0; i < totalMachines; i++) {
                MachineId_t machineId = MachineId_t(i);
                MachineInfo_t machineInfo = Machine_GetInfo(machineId);
                if (machineInfo.cpu != neededCpu) continue;
                if (wantsGpu && !machineInfo.gpus) continue;
                if (machineInfo.s_state != S0 && !machineWaking[machineId]) {
                    Machine_SetState(machineId, S0);
                    machineWaking[machineId] = true;
                    SimOutput("Scheduler::NewTask(EECO): Waking machine "
                              + to_string(machineId) + " for task " + to_string(taskId), 2);
                    pendingTasks.push_back(taskId);
                    return;
                }
				// we can queue tasks while waking up the machines (STILL SUPER COOL WHAAAAAAAAAAAAAAAAAAAA) Lucas look!
                if (machineWaking[machineId]) {
                    pendingTasks.push_back(taskId);
                    return;
                }
            }
            SimOutput("Scheduler::NewTask(EECO): No machine available, queuing task "
                      + to_string(taskId), 1);
            pendingTasks.push_back(taskId);
            return;
        }
		// vm!
        VMId_t targetVm = VMId_t(-1);
        for (VMId_t vmId : machineVms[bestMachine]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                targetVm = vmId;
                break;
            }
        }
        if (targetVm == VMId_t(-1)) {
            targetVm = VM_Create(neededVm, neededCpu);
            VM_Attach(targetVm, bestMachine);
            vms.push_back(targetVm);
            machineVms[bestMachine].push_back(targetVm);
        }
        VM_AddTask(targetVm, taskId, priority);
        taskPlaced[taskId] = true;
		// After distributing the task, we adjust the processor state if the current machine, 
		//this allows us to have an updated EECO score for the next task!
        MachineInfo_t machineInfo = Machine_GetInfo(bestMachine);
        float util = (float)(machineInfo.active_tasks + 1) / (float)machineInfo.num_cpus;
        CPUPerformance_t pState;
        if (util > 0.90f)       pState = P0;
        else if (util > 0.65f)  pState = P1;
        else if (util > 0.35f)  pState = P2;
        else                    pState = P3;
        for (unsigned c = 0; c < machineInfo.num_cpus; c++)
            Machine_SetCorePerformance(bestMachine, c, pState);
        return;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    printStatus(now);

    if (currentAlgo == ROUND_ROBIN) {
        // Repeat passes over the pending tasks until a full pass places NOTHING new
        // This makes sure a task placed mid-loop doesn't block tasks after it
        bool madeProgress = true;

        while (madeProgress && !pendingTasks.empty()) {
            madeProgress = false;
            vector<TaskId_t> stillPending;

            for (TaskId_t pendingId : pendingTasks) {
                if (taskPlaced[pendingId]) continue;

                // figure out what kind of VM, CPU, and priority this task needs
                VMType_t neededVm = RequiredVMType(pendingId);
                CPUType_t neededCpu = RequiredCPUType(pendingId);
                Priority_t priority = slaToPriority(RequiredSLA(pendingId));
                unsigned totalMachines = Machine_GetTotal();

                bool placed = false;
                // Starting at rrCounter, loop through all machines to find the next eligible one
                for (unsigned i = 0; i < totalMachines && !placed; i++) {
                    unsigned idx = (rrCounter + i) % totalMachines;
                    MachineId_t machineId = MachineId_t(idx);
                    MachineInfo_t machineInfo = Machine_GetInfo(machineId);

                    // Skip the machines that are powered off, wrong CPU type, or out of memory
                    if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu 
                        || !machineHasMemory(machineId, pendingId)) {
                        continue;
                    }

                    // Skip machines where all the CPU cores are already busy
                    if (machineInfo.active_tasks >= machineInfo.num_cpus) {
                        continue;
                    }

                    // Try to reuse an existing VM with the right type and CPU on THIS machine
                    VMId_t targetVm = VMId_t(-1);
                    for (VMId_t vmId : machineVms[machineId]) {
                        VMInfo_t vmInfo = VM_GetInfo(vmId);
                        if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                            targetVm = vmId;
                            break;
                        }
                    }

                    // WE DIDNT FIND A VALID VM, create one and attach it to THIS machine
                    if (targetVm == VMId_t(-1)) {
                        targetVm = VM_Create(neededVm, neededCpu);
                        VM_Attach(targetVm, machineId);
                        vms.push_back(targetVm);
                        machineVms[machineId].push_back(targetVm);
                    }

                    // Place the task and move the rr pointer past this machine
                    VM_AddTask(targetVm, pendingId, priority);
                    taskPlaced[pendingId] = true;
                    rrCounter = (idx + 1) % totalMachines;
                    placed = true;
                    madeProgress = true;
                    SimOutput("Scheduler::PeriodicCheck(RR): Placed pending task "
                              + to_string(pendingId), 2);
                }

                if (!placed) {
                    stillPending.push_back(pendingId);
                }
            }

            pendingTasks = stillPending;
        }
        return;
    }

    // only do MINMIN and EECO for everything below
    if (currentAlgo != MINMIN && currentAlgo != EECO) {
        return;
    }

    // MINMIN / EECO
    // Iterate pending tasks and place each on the BEST available machine
    unsigned totalMachines = Machine_GetTotal();
    vector<TaskId_t> stillPending;
    for (TaskId_t pendingId : pendingTasks) {
        if (taskPlaced[pendingId]) {
            continue;
        }

        // Gather the requirements
        VMType_t neededVm = RequiredVMType(pendingId);
        CPUType_t neededCpu = RequiredCPUType(pendingId);
        Priority_t priority = slaToPriority(RequiredSLA(pendingId));
        bool wantsGpu = IsTaskGPUCapable(pendingId);

        MachineId_t bestMachine = MachineId_t(-1);

        if (currentAlgo == MINMIN) {
            // We want to hoose the machine that gives the smallest Estimated Completion Time (ECT)
            double bestEct = 1e18;
            unsigned expectedRuntime = GetTaskInfo(pendingId).target_completion;

            for (unsigned i = 0; i < totalMachines; i++) {
                MachineId_t machineId = MachineId_t(i);
                MachineInfo_t machineInfo = Machine_GetInfo(machineId);
                // Skip machines that are off, wrong CPU, insufficient memory, full, or missing GPU
                if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu) continue;
                if (!machineHasMemory(machineId, pendingId)) continue;
                if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;
                if (wantsGpu && !machineInfo.gpus) continue;

                // ECT estimate, basically higher load or lower MIPS increases the completion time!!
                double mips = (double)machineInfo.performance[0];
                double loadFactor = (double)machineInfo.active_tasks / (double)machineInfo.num_cpus;
                double ect = (1.0 + loadFactor) * (double)expectedRuntime / (mips / 1000.0);

                // Compare ECT to the best ECT so far
                if (ect < bestEct) {
                    bestEct = ect;
                    bestMachine = machineId;
                }
            }
        } else {
            // EECO, basically choose the machine with the highest energy-efficiency score
            int bestScore = -1;
            for (unsigned i = 0; i < totalMachines; i++) {
                MachineId_t machineId = MachineId_t(i);
                MachineInfo_t machineInfo = Machine_GetInfo(machineId);
                // Skip machines (same as MINMIN)
                if (machineInfo.s_state != S0) continue;
                if (machineInfo.cpu != neededCpu) continue;
                if (!machineHasMemory(machineId, pendingId)) continue;
                if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;
                if (wantsGpu && !machineInfo.gpus) continue;

                // Compare current score and best score so far
                int score = computeEecoScore(machineInfo, wantsGpu);
                if (score > bestScore) {
                    bestScore = score;
                    bestMachine = machineId;
                }
            }
        }

        // No active machine was eligible!! 
        // Try and wake a sleeping machine that is COMPATIBLE
        if (bestMachine == MachineId_t(-1)) {
            if (currentAlgo == MINMIN) {
                // Find the first sleeping machine with the right CPU (or GPU is we need) and wake it up
                for (unsigned i = 0; i < totalMachines; i++) {
                    MachineId_t machineId = MachineId_t(i);
                    MachineInfo_t machineInfo = Machine_GetInfo(machineId);

                    if (machineInfo.cpu != neededCpu) continue;
                    if (wantsGpu && !machineInfo.gpus) continue;
                    if (machineWaking[machineId]) continue;  // already in the middle of waking

                    if (machineInfo.s_state != S0) {
                        Machine_SetState(machineId, S0);
                        machineWaking[machineId] = true;
                        SimOutput("Scheduler::PeriodicCheck(MINMIN): Waking machine "
                                  + to_string(machineId) + " for pending task "
                                  + to_string(pendingId), 2);
                        break;
                    }
                }
            }

            if (currentAlgo == EECO) {
                // Same wake-up logic for EECO, basically skip machines that are already waking
                for (unsigned i = 0; i < totalMachines; i++) {
                    MachineId_t machineId = MachineId_t(i);
                    MachineInfo_t machineInfo = Machine_GetInfo(machineId);
                    if (machineInfo.cpu != neededCpu) continue;
                    if (wantsGpu && !machineInfo.gpus) continue;
                    if (machineInfo.s_state != S0 && !machineWaking[machineId]) {
                        Machine_SetState(machineId, S0);
                        machineWaking[machineId] = true;
                        SimOutput("Scheduler::PeriodicCheck(EECO): Waking machine "
                                  + to_string(machineId) + " for pending task "
                                  + to_string(pendingId), 2);
                        break;
                    }
                }
            }

            // Keep the task in the pending list until a machine is ready to go 
            stillPending.push_back(pendingId);
            continue;
        }

        // A machine that aligns was found!
        // Reuse or create a VM on it
        VMId_t targetVm = VMId_t(-1);
        for (VMId_t vmId : machineVms[bestMachine]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                targetVm = vmId;
                break;
            }
        }

        // No compatible VM on this machine :( 
        // Create and attach one
        if (targetVm == VMId_t(-1)) {
            targetVm = VM_Create(neededVm, neededCpu);
            VM_Attach(targetVm, bestMachine);
            vms.push_back(targetVm);
            machineVms[bestMachine].push_back(targetVm);
        }

        VM_AddTask(targetVm, pendingId, priority);
        taskPlaced[pendingId] = true;
        SimOutput("Scheduler::PeriodicCheck(): Placed pending task " + to_string(pendingId), 2);
    }
    pendingTasks = stillPending;

    // After placement, tune CPU performance states on active machines based on their utilization metrics
    if (currentAlgo == EECO) {
        for (unsigned i = 0; i < totalMachines; i++) {
            MachineId_t machineId = MachineId_t(i);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
            // ONLY adjust machines that are fully on, not mid-wake, and have active tasks running
            if (machineInfo.s_state != S0 || machineWaking[machineId] || machineInfo.active_tasks == 0) {
                continue;
            } 

            // Higher utilization means higher performance P-state!! (P0 = max, P3 = min)
            float util = (float)machineInfo.active_tasks / (float)machineInfo.num_cpus;
            CPUPerformance_t pState = (util > 0.90f) ? P0 :
                                      (util > 0.65f) ? P1 :
                                      (util > 0.35f) ? P2 : P3;
            for (unsigned c = 0; c < machineInfo.num_cpus; c++) {
                Machine_SetCorePerformance(machineId, c, pState);
            }
                
        }
    }
}

void Scheduler::Shutdown(Time_t time) {
    // Log that the simulation has finished — no resource cleanup required here
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t taskId) {
    printStatus(now);

    SimOutput("Scheduler::TaskComplete(): Task " + to_string(taskId)
              + " is complete at " + to_string(now), 4);

    // MINMIN and EECO drain their pending queues in PeriodicCheck, NOT HERE
    if (currentAlgo == MINMIN || currentAlgo == EECO) {
        return;
    }

    // Greedy, basically sort pending tasks by SLA strictness before attempting to place them
    // So the most time-sensitive tasks get resources first
    sort(pendingTasks.begin(), pendingTasks.end(), [](TaskId_t a, TaskId_t b) {
        return RequiredSLA(a) < RequiredSLA(b);
    });

    vector<TaskId_t> stillPending;
    for (TaskId_t pendingId : pendingTasks) {
        if (taskPlaced[pendingId]) continue;

        // Gather the task requirements
        VMType_t neededVm = RequiredVMType(pendingId);
        CPUType_t neededCpu = RequiredCPUType(pendingId);
        Priority_t priority = slaToPriority(RequiredSLA(pendingId));
        bool wantsGpu = IsTaskGPUCapable(pendingId);
        unsigned totalMachines = Machine_GetTotal();

        MachineId_t bestMachine = MachineId_t(-1);
        unsigned bestScore = 0;

        // Greedy bin-packing, prefer machines are already heavily loaded
        // Machines with a GPU get a MASSIVE score bonus when the task requires one
        for (unsigned i = 0; i < totalMachines; i++) {
            MachineId_t machineId = MachineId_t(i);
            MachineInfo_t machineInfo = Machine_GetInfo(machineId);
            if (machineInfo.s_state != S0 || machineInfo.cpu != neededCpu
                || !machineHasMemory(machineId, pendingId)) continue;

            if (machineInfo.active_tasks >= machineInfo.num_cpus) continue;

            // Score = memory already used, large bonus if task needs GPU and machine has one
            unsigned score = machineInfo.memory_used;
            if (wantsGpu && machineInfo.gpus) score += 1000000;
            if (score >= bestScore) {
                bestScore = score;
                bestMachine = machineId;
            }
        }

        if (bestMachine == MachineId_t(-1)) {
            stillPending.push_back(pendingId);
            continue;
        }

        // Reuse an existing compatible VM on the chosen machine (or just create one)
        VMId_t targetVm = VMId_t(-1);
        for (VMId_t vmId : machineVms[bestMachine]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            if (vmInfo.vm_type == neededVm && vmInfo.cpu == neededCpu) {
                targetVm = vmId;
                break;
            }
        }

        if (targetVm == VMId_t(-1)) {
            targetVm = VM_Create(neededVm, neededCpu);
            VM_Attach(targetVm, bestMachine);
            vms.push_back(targetVm);
            machineVms[bestMachine].push_back(targetVm);
        }

        VM_AddTask(targetVm, pendingId, priority);
        taskPlaced[pendingId] = true;
        SimOutput("Scheduler::TaskComplete(): Placed pending task "
                  + to_string(pendingId), 1);
    }
    pendingTasks = stillPending;
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vmId) {
    // Clear migration flags so this VM slot and the global migrating flag can be reused
    vmMigrating[vmId] = false;
    migrating = false;
}

// Public interface below
// These free functions are the simulator's entry points; each simply delegates
// to the single global Scheduler instance.

static Scheduler scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t taskId) {
    SimOutput("HandleNewTask(): Received new task " + to_string(taskId)
              + " at time " + to_string(time), 4);
    scheduler.NewTask(time, taskId);
}

void HandleTaskCompletion(Time_t time, TaskId_t taskId) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(taskId)
              + " completed at time " + to_string(time), 4);
    scheduler.TaskComplete(time, taskId);
}

void MemoryWarning(Time_t time, MachineId_t machineId) {
    // Log memory overflow events — no corrective action taken here
    SimOutput("MemoryWarning(): Overflow at " + to_string(machineId)
              + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vmId) {
    SimOutput("MigrationDone(): Migration of VM " + to_string(vmId)
              + " was completed at time " + to_string(time), 4);
    scheduler.MigrationComplete(time, vmId);
}

void SchedulerCheck(Time_t time) {
    // Called by the simulator on a periodic timer to drain the pending task queue
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    // Print final SLA compliance percentages and total cluster energy consumption
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t taskId) {
    // Immediately escalate the at-risk task to HIGH_PRIORITY
    SetTaskPriority(taskId, HIGH_PRIORITY);

    // For MINMIN/EECO only, locate which machine is running this task
    // Boost all its CPU cores to maximum performance (P0) to help it finish in time
    if (currentAlgo != MINMIN && currentAlgo != EECO) {
        return;
    }
    unsigned totalMachines = Machine_GetTotal();
    for (unsigned i = 0; i < totalMachines; i++) {
        MachineId_t machineId = MachineId_t(i);
        MachineInfo_t machineInfo = Machine_GetInfo(machineId);
        if (machineInfo.s_state != S0) {
            continue;
        }
        for (VMId_t vmId : machineVms[machineId]) {
            VMInfo_t vmInfo = VM_GetInfo(vmId);
            for (TaskId_t activeTaskId : vmInfo.active_tasks) {
                if (activeTaskId == taskId) {
                    // Found the machine, set every core to P0 (MAX SPEED)
                    for (unsigned c = 0; c < machineInfo.num_cpus; c++)
                        Machine_SetCorePerformance(machineId, c, P0);
                    SimOutput("SLAWarning(): Boosted machine " + to_string(machineId)
                              + " to P0 for task " + to_string(taskId), 2);
                    return;
                }
            }
        }
    }
}

void StateChangeComplete(Time_t time, MachineId_t machineId) {
    // Once a machine finishes transitioning to S0 (FULLY ON), clear its waking flag
    // So it becomes eligible for task placement in later scheduling passes
    MachineInfo_t machineInfo = Machine_GetInfo(machineId);
    if (machineInfo.s_state == S0) {
        machineWaking[machineId] = false;
    }
}