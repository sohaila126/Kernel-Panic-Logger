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
sys_pipe(void)
{
  uint64 fdarray;
  argaddr(0, &fdarray);
  return kpipe(fdarray);
}

uint64
sys_read(void)
{
  int n;
  uint64 p;
  int f;

  argint(0, &f);
  argaddr(1, &p);
  argint(2, &n);
  return fileread(f, p, n);
}

uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);
  return kkill(pid);
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argstr(0, path, MAXPATH);
  argaddr(1, &uargv);
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      return -1;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      return -1;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0){
      for(i--; i>=0; i--)
        kfree(argv[i]);
      return -1;
    }
  }

  int ret = kexec(path, argv);

  for(i=0; i<NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;
}

uint64
sys_fstat(void)
{
  struct stat st;
  uint64 staddr;
  int f;

  argint(0, &f);
  argaddr(1, &staddr);
  if (filestat(f, (uint64)&st, sizeof(st)) < 0)
    return -1;
  if (copyout(myproc()->pagetable, staddr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  argstr(0, path, MAXPATH);
  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_dup(void)
{
  int f;
  argint(0, &f);
  return kdup(f);
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
  int oldsize;
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
  return kpause(n);
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

uint64
sys_open(void)
{
  char path[MAXPATH];
  int omode;
  argstr(0, path, MAXPATH);
  argint(1, &omode);
  return kopen(path, omode);
}

uint64
sys_write(void)
{
  int n;
  uint64 p;
  int f;

  argint(0, &f);
  argaddr(1, &p);
  argint(2, &n);
  return filewrite(f, p, n);
}

uint64
sys_mknod(void)
{
  char path[MAXPATH];
  int major, minor;

  argstr(0, path, MAXPATH);
  argint(1, &major);
  argint(2, &minor);
  return kmknod(path, major, minor);
}

uint64
sys_unlink(void)
{
  char path[MAXPATH];
  argstr(0, path, MAXPATH);
  return kunlink(path);
}

uint64
sys_link(void)
{
  char old[MAXPATH], new[MAXPATH];
  argstr(0, old, MAXPATH);
  argstr(1, new, MAXPATH);
  return klink(old, new);
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  argstr(0, path, MAXPATH);
  return kmkdir(path);
}

uint64
sys_close(void)
{
  int f;
  argint(0, &f);
  return kclose(f);
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
