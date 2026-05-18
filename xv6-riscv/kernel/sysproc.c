#include "types.h"
#include "riscv.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"
#include "paniclog.h"

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);
  return kkill(pid);
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_sbrk(void)
{
  int n;
  int a;

  argint(0, &n);
  a = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return a;
}

uint64
sys_pause(void)
{
  int n;
  argint(0, &n);
  // pause is handled by trap.c's clock interrupt
  return 0;
}

uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// ---- NEW: sys_dumppanic --------------------------------------------
// Copy the saved crash context to user-space buffer.
// User provides: uint64 dst_addr, uint64 *len_out
// Returns 0 on success, -1 on error.
uint64
sys_dumppanic(void)
{
  uint64 dst;
  uint64 len_addr;

  argaddr(0, &dst);
  argaddr(1, &len_addr);

  struct proc *p = myproc();

  // First write the size of crash_context to *len_addr
  uint64 sz = sizeof(saved_crash_ctx);
  if (copyout(p->pagetable, len_addr, (char*)&sz, sizeof(sz)) < 0)
    return -1;

  // Then copy the crash context itself to user buffer
  if (copyout(p->pagetable, dst, (char*)&saved_crash_ctx, sizeof(saved_crash_ctx)) < 0)
    return -1;

  return 0;
}

// ---- NEW: sys_logtest -------------------------------------------------
// Exercises the kernel log system from user space.
// Returns 0 on success.
uint64
sys_logtest(void)
{
  return log_test();
}
