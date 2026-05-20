//
// Kernel panic logging system.
// Provides a circular log buffer, crash-context capture,
// logging functions (info/warn/debug), and panic-handler
// enhancements.
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

// ============================================================================
// SECTION: Log buffer (circular, statically allocated)
// ============================================================================
static struct {
  struct spinlock    lock;
  struct log_entry   entries[LOG_SIZE];
  int                head;
  int                count;
} logbuf;

// ============================================================================
// SECTION: Saved crash context (statically allocated)
// ============================================================================
struct crash_context saved_crash_ctx;

// ============================================================================
// SECTION: lvlstr — convert log level to string
// ============================================================================
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

// ============================================================================
// SECTION: vsnprintf — minimal string formatter for log buffer
// ============================================================================
static int
vsnprintf(char *buf, int n, const char *fmt, va_list ap)
{
  char *dst = buf;
  char *end = buf + n - 1;
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

// ============================================================================
// SECTION: log_add — internal circular buffer insertion
// ============================================================================
static void
log_add(int level, const char *file, int line, const char *fmt, va_list ap)
{
  acquire(&logbuf.lock);

  struct log_entry *e = &logbuf.entries[logbuf.head];

  e->ticks = ticks;
  e->level = level;

  const char *basename = file;
  for (const char *p = file; *p; p++) {
    if (*p == '/' || *p == '\\')
      basename = p + 1;
  }
  safestrcpy(e->file, (char*)basename, LOG_FILE_MAX);
  e->line = line;

  vsnprintf(e->msg, LOG_MSG_MAX, fmt, ap);

  logbuf.head = (logbuf.head + 1) % LOG_SIZE;
  if (logbuf.count < LOG_SIZE)
    logbuf.count++;

  release(&logbuf.lock);
}

// ============================================================================
// SECTION: log_info — public INFO-level logger
// ============================================================================
void
log_info(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_INFO, file, line, fmt, ap);
  va_end(ap);
}

// ============================================================================
// SECTION: log_warn — public WARN-level logger
// ============================================================================
void
log_warn(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_WARN, file, line, fmt, ap);
  va_end(ap);
}

// ============================================================================
// SECTION: log_debug — public DEBUG-level logger
// ============================================================================
void
log_debug(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_DEBUG, file, line, fmt, ap);
  va_end(ap);
}

// ============================================================================
// SECTION: log_panic_prep — log a PANIC-level entry before panic
// ============================================================================
void
log_panic_prep(const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_add(LOG_PANIC, file, line, fmt, ap);
  va_end(ap);
}

// ============================================================================
// SECTION: log_flush — dump entire circular buffer to console
// ============================================================================
void
log_flush(void)
{
  acquire(&logbuf.lock);

  printf("\n--- LOG FLUSH (most recent first) ---\n");
  int idx = logbuf.head;
  int printed = 0;
  for (int i = 0; i < logbuf.count; i++) {
    idx = (idx - 1 + LOG_SIZE) % LOG_SIZE;
    struct log_entry *e = &logbuf.entries[idx];
    printf("[%5d][%s] %s:%d ", e->ticks, lvlstr(e->level),
           e->file, e->line);
    printf("%s\n", e->msg);
    printed++;
  }
  if (printed == 0)
    printf("(log buffer empty)\n");
  printf("--- END LOG FLUSH ---\n");

  release(&logbuf.lock);
}

// ============================================================================
// SECTION: log_save_crash_context — capture registers, CSRs, process, stack
// ============================================================================
void
log_save_crash_context(const char *panic_msg)
{
  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));

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

  saved_crash_ctx.sepc    = r_sepc();
  saved_crash_ctx.scause  = r_scause();
  saved_crash_ctx.stval   = r_stval();
  saved_crash_ctx.sstatus = r_sstatus();

  acquire(&tickslock);
  saved_crash_ctx.panic_ticks = ticks;
  release(&tickslock);

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

  saved_crash_ctx.cpu = cpuid();

  safestrcpy(saved_crash_ctx.panic_msg, (char*)panic_msg, 128);

  uint64 fp = saved_crash_ctx.s0;
  saved_crash_ctx.stack_depth = 0;
  for (int i = 0; i < 10 && fp != 0; i++) {
    uint64 *frame = (uint64 *)fp;
    saved_crash_ctx.stacktrace[i] = frame[1];
    fp = frame[0];
    saved_crash_ctx.stack_depth++;
  }

  saved_crash_ctx.magic = CRASH_MAGIC;
}

// ============================================================================
// SECTION: log_dump_crash_context — print saved crash context to console
// ============================================================================
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

// ============================================================================
// SECTION: log_init — initialize log buffer and crash context
// ============================================================================
void
log_init(void)
{
  initlock(&logbuf.lock, "logbuf");
  logbuf.head  = 0;
  logbuf.count = 0;

  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));
  saved_crash_ctx.magic = 0;
}

// ============================================================================
// SECTION: log_test — exercise the log system from kernel context
// ============================================================================
int
log_test(void)
{
  int failed = 0;

  log_info(__FILE__, __LINE__, "log_test: INFO message (pid=42)");
  log_warn(__FILE__, __LINE__, "log_test: WARN message (x=%x)", 0xDEAD);
  log_debug(__FILE__, __LINE__, "log_test: DEBUG message (str=%s)", "hello");
  log_panic_prep(__FILE__, __LINE__, "log_test: PANIC prep (num=%d)", -1);

  for (int i = 0; i < LOG_SIZE + 5; i++) {
    log_debug(__FILE__, __LINE__, "wrap test i=%d", i);
  }

  log_info(__FILE__, __LINE__, "fmt d=%d ld=%ld lld=%lld", -1, -2L, -3LL);
  log_info(__FILE__, __LINE__, "fmt u=%u lu=%lu llu=%llu", 1U, 2UL, 3ULL);
  log_info(__FILE__, __LINE__, "fmt x=%x lx=%lx llx=%llx", 0xFF, 0xFFFUL, 0xFFFFULL);
  log_info(__FILE__, __LINE__, "fmt s=\"%s\" c='%c' p=%p", "test", 'A', (void*)0x80000000);
  log_info(__FILE__, __LINE__, "fmt %%");

  log_flush();

  printf("log_test: all tests completed.\n");
  return failed;
}
