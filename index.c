#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
// Hey there! We need these standard headers to talk to the OS.
// Think of them as the toolbox we need before we start building.
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

#define MEMORY_MAX (1 << 16)

// Memory
// This is the VM's RAM. It's just a big array where we store data and code.
// 65536 locations is plenty for what we're doing (hopefully!).
uint16_t memory[MEMORY_MAX];
/* 65536 memory locations in our VM  */

// Registers
// These are like the CPU's pockets. It keeps stuff here that it's working on right now.
// Faster than reaching into memory (the backpack).
enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,     // Program Counter: points to the next instruction. Ideally, it doesn't get lost.
    R_COND,   // Condition Flag: tells us if the last math operation was positive, zero, or negative.
    R_COUNT,
};

uint16_t regs[R_COUNT];

// Define OPCodes
// These are the commands the CPU understands. It's a small vocabulary, but it gets the job done.
enum {
    OP_BR = 0, // Branch: Conditional jump. "If the last thing was zero, go here."
    OP_ADD,    // Add: Math! 1 + 1 = 2 (usually).
    OP_LD,     // Load: Go get data from memory.
    OP_ST,     // Store: Put data into memory.
    OP_JSR,    // Jump Register: Call a function.
    OP_AND,    // Bitwise AND: Masking bits like a pro.
    OP_LDR,    // Load Register: Pointer arithmetic stuff.
    OP_STR,    // Store Register: Writing to where a pointer points.
    OP_RTI,    // Return from Interrupt: We won't use this much, but it's here.
    OP_NOT,    // Bitwise NOT: Flipping bits. 0 becomes 1, cats start barking.
    OP_LDI,    // Load Indirect: Pointer to a pointer. Inception style.
    OP_STI,    // Store Indirect: Writing to a pointer to a pointer.
    OP_JMP,    // Jump: unconditional GOTO. Use responsibly.
    OP_RES,    // Reserved (unused)
    OP_LEA,    // Load Effective Address: "Where is this variable living?"
    OP_TRAP,   // Trap: System call. "Hey OS, do something for me."
};

// Conditional flags
// The CPU's mood ring. It tells us the result of the last calculation.
enum {
    FL_POS = 1 << 0, // Positive
    FL_ZRO = 1 << 1, // Zero
    FL_NEG = 1 << 2, // Negative
};


// Defining trap codes for performing I/O tasks
// These are like system calls. "Hey computer, print this" or "Hey computer, gimme a key press".
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

// Defines memory mapped registers
// Some registers are stored in memory instead of register table
// It's like a secret trapdoor in memory that actually talks to hardware.
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

struct termios original_tio;

// Turning off the "hit enter to send" feature of the terminal.
// We want characters AS SOON AS you type them.
void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

// Cleaning up our mess. Put the terminal back to normal when we're done.
void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

// Checking if a key was pressed without blocking.
// "Hey, anybody there? No? Okay moving on."
uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}


// Reading from memory.
// Usually simple, but if you touch the keyboard address, magic happens.
uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}


// Sign extension: taking a small number and making it fit in a bigger container
// while keeping its value (and sign) correct.
uint16_t sign_extend(uint16_t x, int bit_count) {
    if ((x >> (bit_count - 1) & 1)) {
        x |= (0xFFFF << bit_count);
    }

    return x;
}

// Updating the conditional flag (R_COND) based on the latest result.
// Did we get a zero? A positive? A negative? The CPU needs to know.
void update_flags(uint16_t r) {
    if (regs[r] == 0) {
        regs[R_COND] = FL_ZRO;
    } else if(regs[r] >> 15) {
        regs[R_COND] = FL_NEG;
    } else {
        regs[R_COND] = FL_POS;
    }
}

void mem_write(uint16_t addr, uint16_t val) {
    memory[addr] = val;
}

// LC-3 is big-endian, but most modern computers are little-endian.
// We need to swap bytes so everyone understands each other.
uint16_t swap_16(uint16_t x) {
    return (x << 8) | (x >> 8);
};



void read_image_file(FILE* file) {
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap_16(origin);

    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    while (read-- > 0) {
        *p = swap_16(*p);
        ++p;
    }
}

// Loading the program (ROM image) into memory.
int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };

    read_image_file(file);
    fclose(file);
    return 1;
}



// Catching Ctrl+C so we can exit gracefully and fix the terminal.
void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, const char* argv[]) {
    // Setup the interrupt handler so we can exit cleanly (Ctrl+C).
    signal(SIGINT, handle_interrupt);
    // Fix the terminal input mode.
    disable_input_buffering();

    // Check if the user gave us a program to run.
    if (argc < 2) {
         printf("lc3 [image-file]...\n");
         exit(2);
    }

    // Load the program(s) into memory.
    // Yes, you can load multiple files. They just go into different places in memory.
    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j])) {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    // Resetting the mood ring (condition flag) to zero.
    regs[R_COND] = FL_ZRO;

    // LC-3 programs usually start at address 0x3000. It's just a rule.
    enum { PC_START = 0x3000 };

    // Point the PC to the starting line.
    regs[R_PC] = PC_START;

    // And... we're off!
    int running  = 1;

    while(running) {
        // Fetch the instruction and increment the PC.
        // "What do I do next?"
        uint16_t instr = mem_read(regs[R_PC]++);

        uint16_t op = instr >> 12;

        uint16_t r0, r1, r2, imm_flag, pcoffset;

        // This is where the magic happens. We look at the opcode (the first 4 bits)
        // and decide which operation to execute. It's the heartbeat of the CPU.
        // The documentation of different op codes can be found online
        switch(op) {
            case OP_ADD:
            r0 = (instr >> 9) & 0x7;
            r1 = (instr >> 6) & 0x7;

            imm_flag = (instr >> 5) & 0x1;

            if (imm_flag) {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                regs[r0] = regs[r1] + imm5;
            } else {
                uint16_t r2 = instr & 0x7;
                regs[r0] = regs[r1] + regs[r2];
            }
            update_flags(r0);
            break;
            case OP_AND:
            r0 = (instr >> 9) & 0x7;
            r1 = (instr >> 6) & 0x7;

            imm_flag = (instr >> 5) & 0x1;

            if (imm_flag) {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                regs[r0] = regs[r1] & imm5;
            } else {
                uint16_t r2 = instr & 0x7;
                regs[r0] = regs[r1] & regs[r2];
            }
            update_flags(r0);
            break;
            case OP_NOT:
            r0 = (instr >> 9) & 0x7;
            r1 = (instr >> 6) & 0x7;
            regs[r0] = ~regs[r1];
            update_flags(r0);
            break;
            case OP_BR:
            uint16_t cond_flag = (instr >> 9) & 0x7;
            pcoffset = sign_extend(instr & 0x1FF, 9);
            if (cond_flag & regs[R_COND]) {
                regs[R_PC] += pcoffset;
            }
            break;
            case OP_JMP:
                r1 = (instr >> 6) & 0x7;
                regs[R_PC] = regs[r1];
            break;
            case OP_JSR:
            cond_flag = (instr >> 11) & 1;
            regs[R_R7] = regs[R_PC];
            if (cond_flag == 0) {
                r1 = (instr >> 6) & 0x7;
                regs[R_PC] = regs[r1];
            } else {
                regs[R_PC] += sign_extend(instr & 0x7FF, 11);
            }
            break;
            case OP_LD:
            r0 =  (instr >> 9) & 0x7;
            regs[r0] = mem_read( regs[R_PC] + sign_extend((instr & 0x1FF), 9));
            update_flags(r0);
            break;
            case OP_LDI:
            uint16_t r0 = (instr >> 9) & 0x7;
            pcoffset = sign_extend(instr & 0x1FF, 9);
            regs[r0] = mem_read(mem_read(regs[R_PC] + pcoffset));
            update_flags(r0);
            break;
            case OP_LDR:
            r0 = (instr >> 9) & 0x7;
            r1 = (instr >> 6 & 0x7);
            uint16_t offset_6 = sign_extend(instr & 0x3F, 6);

            regs[r0] = mem_read(regs[r1] + offset_6);
            update_flags(r0);
            break;
            case OP_LEA:
            r0 = (instr >> 9) & 0x7;
            pcoffset = sign_extend(instr & 0x1FF, 9);
            regs[r0] = regs[R_PC] + pcoffset;
            update_flags(r0);
            break;
            case OP_ST:
                r0 = (instr >> 9) & 0x7;
                pcoffset = sign_extend(instr & 0x1FF, 9);
                mem_write(regs[R_PC] + pcoffset, regs[r0]);
            break;
            case OP_STI:
            r0 = (instr >> 9) & 0x7;
            pcoffset = sign_extend(instr & 0x1FF, 9);
            mem_write(mem_read(regs[R_PC] + pcoffset), regs[r0]);
            break;
            case OP_STR:
             r0 = (instr >> 9) & 0x7;
                r1 = (instr >> 6) & 0x7;
                pcoffset = sign_extend(instr & 0x3F, 6);
               mem_write(regs[r1] + pcoffset, regs[r0]);
            break;
            case OP_TRAP:
            regs[R_R7] = regs[R_PC];

            switch (instr & 0xFF) {
                case TRAP_GETC:
                    regs[R_R0] = (uint16_t)getchar();
                    update_flags(R_R0);
                break;
                case TRAP_OUT:
                    putc((char)regs[R_R0], stdout);
                    fflush(stdout);
                break;
                case TRAP_PUTS:
                    uint16_t* c = memory + regs[R_R0];
                    while(*c) {
                        putc((char)*c, stdout);
                        ++c;
                    }
                    fflush(stdout);
                break;
                case TRAP_IN:
                    printf("Enter a character: ");
                    char ch = getchar();
                    putc(ch, stdout);
                    fflush(stdout);
                    regs[R_R0] = (uint16_t)c;
                    update_flags(R_R0);
                break;
                case TRAP_PUTSP:
                    uint16_t* sp = memory + regs[R_R0];
                    while (*sp)
                    {
                        char char1 = (*sp) & 0xFF;
                        putc(char1, stdout);
                        char char2 = (*sp) >> 8;
                        if (char2) putc(char2, stdout);
                        ++sp;
                    }
                    fflush(stdout);
                break;
                case TRAP_HALT:
                    puts("HALT");
                    fflush(stdout);
                    running = 0;
                break;
            }
            break;
            case OP_RES:
            case OP_RTI:
            default:
            // Bad OP_CODE todo
            break;
        }
    };
}
