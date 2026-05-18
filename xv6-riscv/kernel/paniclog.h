#ifndef _PANICLOG_H_
#define _PANICLOG_H_

// Log levels
#define LOG_INFO    0
#define LOG_WARN    1
#define LOG_PANIC   2
#define LOG_DEBUG   3

// Circular buffer size (configurable)
#define LOG_SIZE    64
#define LOG_MSG_MAX 128
#define LOG_FILE_MAX 32

// A single log buffer entry
struct log_entry {
  uint      ticks;       // CPU ticks at log time
  int       level;       // LOG_INFO, LOG_WARN, LOG_PANIC, LOG_DEBUG
  char      file[LOG_FILE_MAX]; // source file name (basename)
  int       line;        // source line number
  char      msg[LOG_MSG_MAX];  // formatted message
};

// Crash context captured at panic time.
// Adapted for RISC-V (xv6-riscv register set).
struct crash_context {
  // --- RISC-V integer registers (from trapframe or inline asm) ---
  uint64 ra;   // return address
  uint64 sp;   // stack pointer
  uint64 gp;   // global pointer
  uint64 tp;   // thread pointer (hartid)
  uint64 t0, t1, t2;
  uint64 s0, s1;          // callee-saved / frame pointer
  uint64 a0, a1, a2, a3, a4, a5, a6, a7;
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  uint64 t3, t4, t5, t6;

  // --- Supervisor CSRs at panic ---
  uint64 sepc;    // exception program counter
  uint64 scause;  // trap cause
  uint64 stval;   // trap value (bad address)
  uint64 sstatus; // supervisor status

  // --- Current process info ---
  int    pid;
  char   pname[16];
  int    pstate;  // enum procstate value

  // --- Stack trace (RISC-V frame pointer walk) ---
  uint64 stacktrace[10];
  int    stack_depth;

  // --- CPU info ---
  int    cpu;

  // --- Panic message ---
  char   panic_msg[128];

  // --- Timestamp ---
  uint64 panic_ticks;

  // --- Magic: CRASH_MAGIC if this struct contains valid data ---
  uint32 magic;
};

#define CRASH_MAGIC 0xDEADBEEF

// Crash context is statically allocated in paniclog.c.
// External access so sys_dump_panic can copy it to user space.
extern struct crash_context saved_crash_ctx;

// Memory-persistent marker address (for "reserved memory region" concept).
// In xv6-riscv we use a static variable, but this address can be
// treated as the conceptual "reserved region" for documentation.
#define CRASH_CONTEXT_RESERVED_BASE 0x6000
#define LOG_BUF_RESERVED_BASE       0x6400

#endif // _PANICLOG_H_
