uint64
sys_dumppanic(void)
{
  uint64 dst;
  uint64 len_addr;

  argaddr(0, &dst);
  argaddr(1, &len_addr);

  struct proc *p = myproc();

  uint64 sz = sizeof(saved_crash_ctx);
  if (copyout(p->pagetable, len_addr, (char*)&sz, sizeof(sz)) < 0)
    return -1;

  if (copyout(p->pagetable, dst, (char*)&saved_crash_ctx, sizeof(saved_crash_ctx)) < 0)
    return -1;

  return 0;
}
