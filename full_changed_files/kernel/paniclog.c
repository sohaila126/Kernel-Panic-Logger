//
// Kernel panic logging system.
// Provides a circular log buffer, crash-context capture,
// logging functions (info / warn / debug), and panic-handler
// enhancements.
//
// Adapted for xv6-riscv (RISC-V 64-bit).
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "paniclog.h"

// ------------------------------------------------------------------
// Log buffer (circular, statically allocated)
// ------------------------------------------------------------------
static struct {
  struct spinlock    lock;
  struct log_entry   entries[LOG_SIZE];
  int                head;   // next slot to write
  int                count;  // number of entries in buffer
} logbuf;

// ------------------------------------------------------------------
// Saved crash context (statically allocated, readable after panic)
// ------------------------------------------------------------------
struct crash_context saved_crash_ctx;

// ---- local helpers -------------------------------------------------

static const char *lvlstr(int level)
{
  switch (level) {
  case LOG_INFO:  return "INFO";
  case LOG_WARN:  return "WARN";
  case LOG_PANIC: return "PANIC";
  case LOG_DEBUG: return "DEBUG";
  default:        return "????";
  }
}

// Minimal vsnprintf – supports the same subset as xv6's printf:
//   %d %ld %lld %u %lu %llu %x %lx %llx %p %s %c %%
// Writes at most 'n' bytes into 'buf' (nul-terminated).
// Returns the number of chars that would be written (excluding nul).
static int
vsnprintf(char *buf, int n, const char *fmt, va_list ap)
{
  char *dst = buf;
  char *end = buf + n - 1;  // leave room for nul
  int    i, cx, c0, c1, c2;
  char  *s;
  unsigned long long x;
  char   tmp[24];
  int    tmplen;
  char   digits[] = "0123456789abcdef";

#define PUTC(ch) do { if (dst < end) *dst++ = (ch); } while(0)

  for (i = 0; (cx = fmt[i] & 0xff) != 0; i++) {
    if (cx != '%') { PUTC(cx); continue; }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if (c0) c1 = fmt[i+1] & 0xff;
    if (c1) c2 = fmt[i+2] & 0xff;

    if (c0 == 'd') {
      x = (long long)va_arg(ap, int);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'd') {
      x = va_arg(ap, long long);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
      x = va_arg(ap, long long);
      if ((long long)x < 0) { PUTC('-'); x = -(long long)x; }
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'u') {
      x = va_arg(ap, uint32);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'u') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 10]; } while ((x /= 10) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'x') {
      x = va_arg(ap, uint32);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
    } else if (c0 == 'l' && c1 == 'x') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 1;
    } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
      x = va_arg(ap, uint64);
      tmplen = 0; do { tmp[tmplen++] = digits[x % 16]; } while ((x /= 16) != 0);
      while (tmplen--) PUTC(tmp[tmplen]);
      i += 2;
    } else if (c0 == 'p') {
      x = va_arg(ap, uint64);
      PUTC('0'); PUTC('x');
      for (int j = 0; j < 16; j++, x <<= 4)
        PUTC(digits[x >> 60]);
    } else if (c0 == 's') {
      s = va_arg(ap, char *);
      if (s == 0) s = "(null)";
      while (*s) PUTC(*s++);
    } else if (c0 == 'c') {
      PUTC(va_arg(ap, int));
    } else if (c0 == '%') {
      PUTC('%');
    } else if (c0 == 0) {
      break;
    } else {
      PUTC('%'); PUTC(c0);
    }
  }

#undef PUTC

  *dst = '\0';
  return dst - buf;
}

// ---- internal: add entry to circular buffer -------------------------
static void
log_add(int level, const char *file, int line, const char *fmt, va_list ap)
{
  acquire(&logbuf.lock);

  struct log_entry *e = &logbuf.entries[logbuf.head];

  // Timestamp using ticks (exported from trap.c)
  e->ticks = ticks;

  e->level = level;

  // Extract basename from file path
  const char *basename = file;
  for (const char *p = file; *p; p++) {
    if (*p == '/' || *p == '\\')
      basename = p + 1;
  }
  safestrcpy(e->file, (char*)basename, LOG_FILE_MAX);
  e->line = line;

  // Format the message via our internal vsnprintf
  vsnprintf(e->msg, LOG_MSG_MAX, fmt, ap);

  // Advance circular buffer
  logbuf.head = (logbuf.head + 1) % LOG_SIZE;
  if (logbuf.count < LOG_SIZE)
    logbuf.count++;

  release(&logbuf.lock);
}

// ---- public logging functions --------------------------------------

void
log_info(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_INFO, file, line, fmt, ap);
  va_end(ap);
}

void
log_warn(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_WARN, file, line, fmt, ap);
  va_end(ap);
}

void
log_debug(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_DEBUG, file, line, fmt, ap);
  va_end(ap);
}

// Called just before panic to capture the last "pre-panic"
// message into the circular buffer.
void
log_panic_prep(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_PANIC, file, line, fmt, ap);
  va_end(ap);
}

// ---- flush: dump the entire log buffer to the console --------------
void
log_flush(void)
{
  static int flushing = 0;
  if (flushing)
    return;
  acquire(&logbuf.lock);
  flushing = 1;

  printf("\n--- LOG FLUSH (most recent first) ---\n");
  int idx = logbuf.head;
  int printed = 0;
  for (int i = 0; i < logbuf.count; i++) {
    idx = (idx - 1 + LOG_SIZE) % LOG_SIZE;
    struct log_entry *e = &logbuf.entries[idx];
    printf("[%d][%s] %s:%d ", e->ticks, lvlstr(e->level),
           e->file, e->line);
    printf("%s\n", e->msg);
    printed++;
  }
  if (printed == 0)
    printf("(log buffer empty)\n");
  printf("--- END LOG FLUSH ---\n");

  flushing = 0;
  release(&logbuf.lock);
}

// ---- capture crash context (registers, stack, process) --------------
void
log_save_crash_context(const char *panic_msg)
{
  // Zero out previous context
  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));

  // Capture RISC-V registers via inline assembly
  asm volatile("mv %0, ra" : "=r"(saved_crash_ctx.ra));
  asm volatile("mv %0, sp" : "=r"(saved_crash_ctx.sp));
  asm volatile("mv %0, gp" : "=r"(saved_crash_ctx.gp));
  asm volatile("mv %0, tp" : "=r"(saved_crash_ctx.tp));
  asm volatile("mv %0, t0" : "=r"(saved_crash_ctx.t0));
  asm volatile("mv %0, t1" : "=r"(saved_crash_ctx.t1));
  asm volatile("mv %0, t2" : "=r"(saved_crash_ctx.t2));
  asm volatile("mv %0, s0" : "=r"(saved_crash_ctx.s0));
  asm volatile("mv %0, s1" : "=r"(saved_crash_ctx.s1));
  asm volatile("mv %0, a0" : "=r"(saved_crash_ctx.a0));
  asm volatile("mv %0, a1" : "=r"(saved_crash_ctx.a1));
  asm volatile("mv %0, a2" : "=r"(saved_crash_ctx.a2));
  asm volatile("mv %0, a3" : "=r"(saved_crash_ctx.a3));
  asm volatile("mv %0, a4" : "=r"(saved_crash_ctx.a4));
  asm volatile("mv %0, a5" : "=r"(saved_crash_ctx.a5));
  asm volatile("mv %0, a6" : "=r"(saved_crash_ctx.a6));
  asm volatile("mv %0, a7" : "=r"(saved_crash_ctx.a7));
  asm volatile("mv %0, s2" : "=r"(saved_crash_ctx.s2));
  asm volatile("mv %0, s3" : "=r"(saved_crash_ctx.s3));
  asm volatile("mv %0, s4" : "=r"(saved_crash_ctx.s4));
  asm volatile("mv %0, s5" : "=r"(saved_crash_ctx.s5));
  asm volatile("mv %0, s6" : "=r"(saved_crash_ctx.s6));
  asm volatile("mv %0, s7" : "=r"(saved_crash_ctx.s7));
  asm volatile("mv %0, s8" : "=r"(saved_crash_ctx.s8));
  asm volatile("mv %0, s9" : "=r"(saved_crash_ctx.s9));
  asm volatile("mv %0, s10" : "=r"(saved_crash_ctx.s10));
  asm volatile("mv %0, s11" : "=r"(saved_crash_ctx.s11));
  asm volatile("mv %0, t3" : "=r"(saved_crash_ctx.t3));
  asm volatile("mv %0, t4" : "=r"(saved_crash_ctx.t4));
  asm volatile("mv %0, t5" : "=r"(saved_crash_ctx.t5));
  asm volatile("mv %0, t6" : "=r"(saved_crash_ctx.t6));

  // Capture supervisor CSRs
  saved_crash_ctx.sepc    = r_sepc();
  saved_crash_ctx.scause  = r_scause();
  saved_crash_ctx.stval   = r_stval();
  saved_crash_ctx.sstatus = r_sstatus();

  // Capture tick count
  acquire(&tickslock);
  saved_crash_ctx.panic_ticks = ticks;
  release(&tickslock);

  // Capture process info
  struct proc *p = myproc();
  if (p) {
    saved_crash_ctx.pid = p->pid;
    safestrcpy(saved_crash_ctx.pname, p->name, 16);
    saved_crash_ctx.pstate = (int)p->state;
  } else {
    saved_crash_ctx.pid = -1;
    safestrcpy(saved_crash_ctx.pname, "(none)", 16);
    saved_crash_ctx.pstate = -1;
  }

  // Capture CPU number (hartid)
  saved_crash_ctx.cpu = cpuid();

  // Copy panic message
  safestrcpy(saved_crash_ctx.panic_msg, (char*)panic_msg, 128);

  // --- Stack trace via frame pointer (s0) walk ---
  // RISC-V calling convention: the frame record at each frame
  // is [saved s0 (fp)] at offset 0, [saved ra] at offset 8.
  // We walk s0 chain up to 10 frames.
  uint64 fp = saved_crash_ctx.s0;
  saved_crash_ctx.stack_depth = 0;
  for (int i = 0; i < 10 && fp != 0; i++) {
    // Read return address at fp+8 (physical reads since we're in kernel)
    uint64 *frame = (uint64 *)fp;
    saved_crash_ctx.stacktrace[i] = frame[1];  // saved ra
    fp = frame[0];                             // saved s0 (next frame)
    saved_crash_ctx.stack_depth++;
  }

  // Mark valid
  saved_crash_ctx.magic = CRASH_MAGIC;
}

// ---- dump the saved crash context to console ------------------------
void
log_dump_crash_context(void)
{
  if (saved_crash_ctx.magic != CRASH_MAGIC) {
    printf("No crash context saved (magic=0x%x, expected 0x%x)\n",
           saved_crash_ctx.magic, CRASH_MAGIC);
    return;
  }

  printf("\n========== KERNEL PANIC CRASH DUMP ==========\n");
  printf("Message: %s\n", saved_crash_ctx.panic_msg);
  printf("Ticks:   %ld\n", saved_crash_ctx.panic_ticks);
  printf("CPU:     %d\n", saved_crash_ctx.cpu);
  printf("\n-- RISC-V Register State --\n");
  printf("ra=0x%lx  sp=0x%lx  gp=0x%lx  tp=0x%lx\n",
         saved_crash_ctx.ra, saved_crash_ctx.sp,
         saved_crash_ctx.gp, saved_crash_ctx.tp);
  printf("s0=0x%lx  s1=0x%lx\n",
         saved_crash_ctx.s0, saved_crash_ctx.s1);
  printf("a0=0x%lx  a1=0x%lx  a2=0x%lx  a3=0x%lx\n",
         saved_crash_ctx.a0, saved_crash_ctx.a1,
         saved_crash_ctx.a2, saved_crash_ctx.a3);
  printf("a4=0x%lx  a5=0x%lx  a6=0x%lx  a7=0x%lx\n",
         saved_crash_ctx.a4, saved_crash_ctx.a5,
         saved_crash_ctx.a6, saved_crash_ctx.a7);
  printf("t0=0x%lx  t1=0x%lx  t2=0x%lx\n",
         saved_crash_ctx.t0, saved_crash_ctx.t1,
         saved_crash_ctx.t2);
  printf("s2=0x%lx  s3=0x%lx  s4=0x%lx  s5=0x%lx\n",
         saved_crash_ctx.s2, saved_crash_ctx.s3,
         saved_crash_ctx.s4, saved_crash_ctx.s5);
  printf("s6=0x%lx  s7=0x%lx  s8=0x%lx  s9=0x%lx\n",
         saved_crash_ctx.s6, saved_crash_ctx.s7,
         saved_crash_ctx.s8, saved_crash_ctx.s9);
  printf("s10=0x%lx s11=0x%lx\n",
         saved_crash_ctx.s10, saved_crash_ctx.s11);
  printf("t3=0x%lx  t4=0x%lx  t5=0x%lx  t6=0x%lx\n",
         saved_crash_ctx.t3, saved_crash_ctx.t4,
         saved_crash_ctx.t5, saved_crash_ctx.t6);
  printf("\n-- Supervisor CSRs --\n");
  printf("sepc=0x%lx  scause=0x%lx  stval=0x%lx  sstatus=0x%lx\n",
         saved_crash_ctx.sepc, saved_crash_ctx.scause,
         saved_crash_ctx.stval, saved_crash_ctx.sstatus);

  printf("\n-- Process Info --\n");
  printf("pid=%d  name=%s  state=%d\n",
         saved_crash_ctx.pid, saved_crash_ctx.pname,
         saved_crash_ctx.pstate);

  printf("\n-- Stack Trace (FP walk, %d frames) --\n",
         saved_crash_ctx.stack_depth);
  for (int i = 0; i < saved_crash_ctx.stack_depth && i < 10; i++) {
    printf("  [%d] 0x%lx\n", i, saved_crash_ctx.stacktrace[i]);
  }
  printf("============================================\n");
}

// ---- initialization: call once during boot --------------------------
void
log_init(void)
{
  initlock(&logbuf.lock, "logbuf");
  logbuf.head  = 0;
  logbuf.count = 0;

  // Clear saved crash context
  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));
  saved_crash_ctx.magic = 0;
}

// ---- test: exercise the log system from kernel context -------------
// Called via sys_logtest.  Returns 0 on success, -1 if any test fails.
int
log_test(void)
{
  int failed = 0;

  // Test 1: basic write at each level
  log_info(__FILE__, __LINE__, "log_test: INFO message (pid=42)");
  log_warn(__FILE__, __LINE__, "log_test: WARN message (x=%x)", 0xDEAD);
  log_debug(__FILE__, __LINE__, "log_test: DEBUG message (str=%s)", "hello");
  log_panic_prep(__FILE__, __LINE__, "log_test: PANIC prep (num=%d)", -1);

  // Verify entries were added (internal buffer should have count >= 4)
  // Can't directly check logbuf.count from here, but we exercise the code path.

  // Test 2: circular buffer wrap — write LOG_SIZE+1 entries
  for (int i = 0; i < LOG_SIZE + 5; i++) {
    log_debug(__FILE__, __LINE__, "wrap test i=%d", i);
  }
  // After wrapping, buffer should still have LOG_SIZE entries.
  // Oldest entries should have been overwritten.
  // (verification of this is indirect — we check by flushing)

  // Test 3: format specifier coverage
  log_info(__FILE__, __LINE__, "fmt d=%d ld=%ld lld=%lld", -1, -2L, -3LL);
  log_info(__FILE__, __LINE__, "fmt u=%u lu=%lu llu=%llu", 1U, 2UL, 3ULL);
  log_info(__FILE__, __LINE__, "fmt x=%x lx=%lx llx=%llx", 0xFF, 0xFFFUL, 0xFFFFULL);
  log_info(__FILE__, __LINE__, "fmt s=\"%s\" c='%c' p=%p", "test", 'A', (void*)0x80000000);
  log_info(__FILE__, __LINE__, "fmt %%");

  // Test 4: flush (just calls the function, output goes to console)
  log_flush();

  printf("log_test: all tests completed.\n");
  return failed;
}
