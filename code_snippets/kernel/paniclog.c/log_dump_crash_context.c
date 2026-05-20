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
