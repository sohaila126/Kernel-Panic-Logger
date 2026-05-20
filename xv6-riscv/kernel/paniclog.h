#ifndef _PANICLOG_H_
#define _PANICLOG_H_

// ============================================================================
// SECTION: Log levels
// ============================================================================
#define LOG_INFO    0
#define LOG_WARN    1
#define LOG_PANIC   2
#define LOG_DEBUG   3

// ============================================================================
// SECTION: Buffer configuration
// ============================================================================
#define LOG_SIZE     64
#define LOG_MSG_MAX  128
#define LOG_FILE_MAX 32

// ============================================================================
// SECTION: struct log_entry — single circular buffer slot
// ============================================================================
struct log_entry {
  uint      ticks;
  int       level;
  char      file[LOG_FILE_MAX];
  int       line;
  char      msg[LOG_MSG_MAX];
};

// ============================================================================
// SECTION: struct crash_context — full machine state at panic
// ============================================================================
struct crash_context {
  uint64 ra, sp, gp, tp;
  uint64 t0, t1, t2;
  uint64 s0, s1;
  uint64 a0, a1, a2, a3, a4, a5, a6, a7;
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  uint64 t3, t4, t5, t6;
  uint64 sepc, scause, stval, sstatus;
  int    pid;
  char   pname[16];
  int    pstate;
  uint64 stacktrace[10];
  int    stack_depth;
  int    cpu;
  char   panic_msg[128];
  uint64 panic_ticks;
  uint32 magic;
};

// ============================================================================
// SECTION: Constants and externs
// ============================================================================
#define CRASH_MAGIC 0xDEADBEEF

extern struct crash_context saved_crash_ctx;

#define CRASH_CONTEXT_RESERVED_BASE 0x6000
#define LOG_BUF_RESERVED_BASE       0x6400

#endif // _PANICLOG_H_
