// ============================================================
// FILE: kernel/paniclog.c (NEW FILE)
// Lines: 1-295 (complete file)
// ============================================================
//
// Implements the following (snippet of key functions):
//
// 1. vsnprintf() — internal mini-printf that writes to a buffer
// 2. log_add() — adds entry to circular log buffer with spinlock
// 3. log_info/log_warn/log_debug/log_panic_prep — public API
// 4. log_flush() — dumps entire circular buffer to console
// 5. log_save_crash_context() — captures all registers, CSRs,
//    process info, and walks frame pointer for stack trace
// 6. log_dump_crash_context() — prints saved crash context
// 7. log_init() — initializes buffer and clears context
//
// ---- Snippet: log_add() - internal circular buffer writer ----
static void
log_add(int level, const char *file, int line, const char *fmt, va_list ap)
{
  acquire(&logbuf.lock);

  struct log_entry *e = &logbuf.entries[logbuf.head];
  e->ticks = ticks;
  e->level = level;

  // Extract basename
  const char *basename = file;
  for (const char *p = file; *p; p++) {
    if (*p == '/' || *p == '\\') basename = p + 1;
  }
  safestrcpy(e->file, (char*)basename, LOG_FILE_MAX);
  e->line = line;

  vsnprintf(e->msg, LOG_MSG_MAX, fmt, ap);

  logbuf.head = (logbuf.head + 1) % LOG_SIZE;
  if (logbuf.count < LOG_SIZE) logbuf.count++;

  release(&logbuf.lock);
}

// ---- Snippet: log_save_crash_context() - captures state at panic ----
void
log_save_crash_context(const char *panic_msg)
{
  memset(&saved_crash_ctx, 0, sizeof(saved_crash_ctx));

  // Read RISC-V registers via inline asm
  asm volatile("mv %0, ra" : "=r"(saved_crash_ctx.ra));
  asm volatile("mv %0, sp" : "=r"(saved_crash_ctx.sp));
  asm volatile("mv %0, gp" : "=r"(saved_crash_ctx.gp));
  asm volatile("mv %0, tp" : "=r"(saved_crash_ctx.tp));
  // ... (all other registers: t0-t6, s0-s11, a0-a7)

  // Read supervisor CSRs
  saved_crash_ctx.sepc    = r_sepc();
  saved_crash_ctx.scause  = r_scause();
  saved_crash_ctx.stval   = r_stval();
  saved_crash_ctx.sstatus = r_sstatus();

  // Timestamp
  acquire(&tickslock);
  saved_crash_ctx.panic_ticks = ticks;
  release(&tickslock);

  // Process info
  struct proc *p = myproc();
  if (p) {
    saved_crash_ctx.pid = p->pid;
    safestrcpy(saved_crash_ctx.pname, p->name, 16);
    saved_crash_ctx.pstate = (int)p->state;
  }

  // CPU / hart
  saved_crash_ctx.cpu = cpuid();
  safestrcpy(saved_crash_ctx.panic_msg, (char*)panic_msg, 128);

  // --- Stack trace: walk RISC-V frame pointer chain ---
  // RISC-V frame record: [saved s0(fp)][saved ra]
  uint64 fp = saved_crash_ctx.s0;
  saved_crash_ctx.stack_depth = 0;
  for (int i = 0; i < 10 && fp != 0; i++) {
    uint64 *frame = (uint64 *)fp;
    saved_crash_ctx.stacktrace[i] = frame[1]; // saved ra
    fp = frame[0];                             // saved s0 (next fp)
    saved_crash_ctx.stack_depth++;
  }

  saved_crash_ctx.magic = CRASH_MAGIC;
}

// ---- Snippet: log_flush() - dump log buffer to console ----
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
