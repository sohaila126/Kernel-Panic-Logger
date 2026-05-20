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
