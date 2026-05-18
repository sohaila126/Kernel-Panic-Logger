// ============================================================
// FILE: kernel/paniclog.h (NEW FILE)
// Lines: 1-62
// ============================================================
//
// New header defining:
//   - Log levels (LOG_INFO, LOG_WARN, LOG_PANIC, LOG_DEBUG)
//   - struct log_entry (circular buffer entry with tick, level, file, line, msg)
//   - struct crash_context (RISC-V registers, CSRs, process info, stack trace)
//   - CRASH_MAGIC marker for context validation
//
#ifndef _PANICLOG_H_
#define _PANICLOG_H_

#define LOG_INFO    0
#define LOG_WARN    1
#define LOG_PANIC   2
#define LOG_DEBUG   3

#define LOG_SIZE    64
#define LOG_MSG_MAX 128
#define LOG_FILE_MAX 32

struct log_entry {
  uint      ticks;
  int       level;
  char      file[LOG_FILE_MAX];
  int       line;
  char      msg[LOG_MSG_MAX];
};

struct crash_context {
  // RISC-V integer registers
  uint64 ra, sp, gp, tp;
  uint64 t0, t1, t2;
  uint64 s0, s1;
  uint64 a0, a1, a2, a3, a4, a5, a6, a7;
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  uint64 t3, t4, t5, t6;

  // Supervisor CSRs
  uint64 sepc, scause, stval, sstatus;

  // Process info
  int    pid;
  char   pname[16];
  int    pstate;

  // Stack trace (RISC-V FP chain walk)
  uint64 stacktrace[10];
  int    stack_depth;

  int    cpu;
  char   panic_msg[128];
  uint64 panic_ticks;
  uint32 magic;
};

#define CRASH_MAGIC 0xDEADBEEF

extern struct crash_context saved_crash_ctx;

#endif
