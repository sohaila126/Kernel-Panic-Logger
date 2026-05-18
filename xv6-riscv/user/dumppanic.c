//
// dumppanic — user-space utility to retrieve and display the
// last kernel panic's crash context.
//
// Usage: dumppanic
//

#include "kernel/paniclog.h"
#include "kernel/types.h"
#include "user/user.h"

static void
print_ctx(struct crash_context *ctx)
{
  printf("\n========== SAVED KERNEL PANIC CRASH DUMP ==========\n");

  if (ctx->magic != CRASH_MAGIC) {
    printf("(No valid crash context found. magic=0x%x, expected 0x%x)\n",
           ctx->magic, CRASH_MAGIC);
    return;
  }

  printf("Message: %s\n", ctx->panic_msg);
  printf("Ticks:   %ld\n", ctx->panic_ticks);
  printf("CPU:     %d\n", ctx->cpu);
  printf("\n-- RISC-V Register State --\n");
  printf("ra=0x%lx  sp=0x%lx  gp=0x%lx  tp=0x%lx\n",
         ctx->ra, ctx->sp, ctx->gp, ctx->tp);
  printf("s0=0x%lx  s1=0x%lx\n", ctx->s0, ctx->s1);
  printf("a0=0x%lx  a1=0x%lx  a2=0x%lx  a3=0x%lx\n",
         ctx->a0, ctx->a1, ctx->a2, ctx->a3);
  printf("a4=0x%lx  a5=0x%lx  a6=0x%lx  a7=0x%lx\n",
         ctx->a4, ctx->a5, ctx->a6, ctx->a7);
  printf("t0=0x%lx  t1=0x%lx  t2=0x%lx\n",
         ctx->t0, ctx->t1, ctx->t2);
  printf("s2=0x%lx  s3=0x%lx  s4=0x%lx  s5=0x%lx\n",
         ctx->s2, ctx->s3, ctx->s4, ctx->s5);
  printf("s6=0x%lx  s7=0x%lx  s8=0x%lx  s9=0x%lx\n",
         ctx->s6, ctx->s7, ctx->s8, ctx->s9);
  printf("s10=0x%lx s11=0x%lx\n", ctx->s10, ctx->s11);
  printf("t3=0x%lx  t4=0x%lx  t5=0x%lx  t6=0x%lx\n",
         ctx->t3, ctx->t4, ctx->t5, ctx->t6);
  printf("\n-- Supervisor CSRs --\n");
  printf("sepc=0x%lx  scause=0x%lx  stval=0x%lx  sstatus=0x%lx\n",
         ctx->sepc, ctx->scause, ctx->stval, ctx->sstatus);
  printf("\n-- Process Info --\n");
  printf("pid=%d  name=%s  state=%d\n",
         ctx->pid, ctx->pname, ctx->pstate);

  printf("\n-- Stack Trace (FP walk, %d frames) --\n", ctx->stack_depth);
  for (int i = 0; i < ctx->stack_depth && i < 10; i++)
    printf("  [%d] 0x%lx\n", i, ctx->stacktrace[i]);

  printf("====================================================\n");
}

int
main(int argc, char *argv[])
{
  // We need a buffer large enough for crash_context.
  // Use a static buffer on the stack.
  struct crash_context ctx;
  uint64 sz;

  int ret = dumppanic(&ctx, &sz);
  if (ret < 0) {
    printf("dumppanic: syscall failed\n");
    return -1;
  }

  if (sz != sizeof(ctx)) {
    printf("dumppanic: size mismatch (expected %ld, got %ld)\n",
           sizeof(ctx), sz);
    return -1;
  }

  print_ctx(&ctx);
  return 0;
}
