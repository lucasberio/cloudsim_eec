# Machines!!!!

# X86, High energy, no gpu, high performance machines
machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [1000, 800, 600, 400]
        GPUs: no
}

# ARM, Low energy, no gpu, low performance machines
machine class:
{
        Number of machines: 24
        CPU type: ARM
        Number of cores: 16
        Memory: 8192
        S-States: [60, 45, 45, 30, 15, 5, 0]
        P-States: [6, 4, 3, 1]
        C-States: [6, 2, 1, 0]
        MIPS: [700, 500, 350, 150]
        GPUs: no
}

# X86, High energy, has gpu, high performance machines
machine class:
{
        Number of machines: 4
        CPU type: X86
        Number of cores: 8
        Memory: 32768
        S-States: [250, 210, 210, 160, 90, 25, 0]
        P-States: [30, 22, 14, 8]
        C-States: [30, 10, 4, 0]
        MIPS: [1200, 950, 700, 450]
        GPUs: yes
}

# ARM, Medium energy, has gpu, hybrid machines
machine class:
{
        Number of machines: 8
        CPU type: ARM
        Number of cores: 16
        Memory: 32768
        S-States: [150, 120, 120, 85, 40, 10, 0]
        P-States: [18, 12, 8, 4]
        C-States: [18, 6, 2, 0]
        MIPS: [900, 700, 500, 300]
        GPUs: yes
}




# Tasks!!!!

# Task 1: Low-intensity warm-up (X86)
# Window: 500,000ms | Inter arrival: 50,000ms | ~10 tasks
# Small WEB requests, SLA2 so the scheduler can relax and consolidate
task class:
{
        Start time: 0
        End time: 500000
        Inter arrival: 50000
        Expected runtime: 150000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 12345
}

# Task 2: Normal baseline (X86)
# Window: 1,000,000ms | Inter arrival: 20000ms | ~50 tasks
# Moderate STREAM workload, SLA1 deadlines, gives scheduler breathing room
task class:
{
        Start time: 500000
        End time: 1500000
        Inter arrival: 20000
        Expected runtime: 400000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: STREAM
        Seed: 100
}

# Task 3: Normal baseline (ARM)
# Window: 1,000,000ms | Inter arrival: 25,000ms | ~40 tasks
# Runs same time as Task 2, SLA3 best-effort, ARM machines absorb these
task class:
{
        Start time: 500000
        End time: 1500000
        Inter arrival: 25000
        Expected runtime: 400000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: ARM
        Task type: STREAM
        Seed: 200
}

# Task 4: Surge — X86 no GPU (CRYPTO)
# Window: 500,000ms | Inter arrival: 5,000ms | ~100 tasks
# SLA0 tight deadline, tests scheduler waking machines fast enough
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 5000
        Expected runtime: 300000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: CRYPTO
        Seed: 101
}

# Task 5: Surge — X86 GPU (AI)
# Window: 500,000ms | Inter arrival: 8,000ms | ~62 tasks
# Routes only to 4 X86 GPU machines, SLA0 tight deadline
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 8000
        Expected runtime: 300000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA0
        CPU type: X86
        Task type: AI
        Seed: 102
}

# Task 6: Surge — ARM GPU (AI)
# Window: 500,000ms | Inter arrival: 10,000ms | ~50 tasks
# Routes to 8 ARM GPU machines, SLA1 slightly relaxed
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 10000
        Expected runtime: 300000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: ARM
        Task type: AI
        Seed: 103
}

# Task 7: Cooldown
# Window: 800,000ms | Inter arrival: 50,000ms | ~16 tasks
# Trickle of WEB tasks, SLA3 best effort
# Scheduler should sleep idle machines aggressively here
task class:
{
        Start time: 2000000
        End time: 2800000
        Inter arrival: 50000
        Expected runtime: 150000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: X86
        Task type: WEB
        Seed: 300
}