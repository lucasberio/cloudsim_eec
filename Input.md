# === Machines!!!! ===

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

# x86, High energy, has gpu, high performance machines
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

# ARM, Medium energy, has gpu, funky hybrid machines
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






# === Tasks!!!!! ===

# First Task (ARM)
# A bunch of small requests (WEB), around 20 tasks, SLA2 for chill, small tasks
task class:
{
        Start time: 0
        End time: 500000
        Inter arrival: 25000
        Expected runtime: 150000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 12345
}

# Second Task (x86)
# Compute intensive tasks, short bursts (STREAM), around 125 tasks, SLA1 for reaching teh deadlines 
task class:
{
        Start time: 500000
        End time: 1500000
        Inter arrival: 8000
        Expected runtime: 600000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: STREAM
        Seed: 100
}

# Third Task (ARM)
# Around the same as task 2 but for ARM, SLA3 to complete whenever however, aronnd 100 tasks
# Runs at the same time as the x86 Task before
task class:
{
        Start time: 500000
        End time: 1500000
        Inter arrival: 10000
        Expected runtime: 600000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: ARM
        Task type: STREAM
        Seed: 200
}

# Fourth Task (x86)
# Short, repetitive, high amount of tasks (CRYPTO), uses the GPU, around 500 tasks, SLA0 for asap
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 1000
        Expected runtime: 300000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: CRYPTO
        Seed: 101
}

# Fifth Task (x86)
# compute intensive, GPU included, (AI), around 167 tasks, SLA0 FINISH ASAP
# Runs at the same time as Task 4
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 3000
        Expected runtime: 300000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA0
        CPU type: X86
        Task type: AI
        Seed: 102
}

# Sixth Task (ARM)
# Also I task, high computation, around 125 tasks, uses GPU but relaxes for SLA1 for routing 
# Same time as tasks 4 and 5
task class:
{
        Start time: 1500000
        End time: 2000000
        Inter arrival: 4000
        Expected runtime: 300000
        Memory: 16
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: ARM
        Task type: AI
        Seed: 103
}

# Seveth Task (X86)
# Cooldown web jobs, low-rate SLA3, around 40 tasks, should sleep idle machines
task class:
{
        End time: 2800000
        Inter arrival: 20000
        Expected runtime: 150000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA3
        CPU type: X86
        Task type: WEB
        Seed: 300
}