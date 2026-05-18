# xv6 Changes for Kernel Panic Logger

This document lists every change that must be applied to the xv6-riscv source tree.

## New Files

| File | Purpose |
|------|---------|
| `kernel/paniclog.h` | Header: log levels, `struct log_entry`, `struct crash_context`, macros |
| `kernel/paniclog.c` | Implementation: circular buffer, logging functions, crash-capture, stack walk |
| `user/dumppanic.c` | User-space utility: retrieves and displays last crash context |
| `user/logtest.c` | Automated test program: exercises log system via syscall, validates crash context |

## Modified Files

### kernel/printf.c
- **Added `#include "paniclog.h"`** at top
- **Enhanced `panic()`** (lines 136-154):
  - Calls `log_save_crash_context(s)` immediately (before any output)
  - Calls `log_flush()` to dump pending log entries
  - Original `printf("panic: ...")` output preserved
  - New call to `log_dump_crash_context()` before entering infinite loop

### kernel/main.c
- **Added call to `log_init()`** right after `printfinit()` on CPU 0 boot path

### kernel/defs.h
- **Added declarations** for all new paniclog functions after the `// log.c` section

### kernel/syscall.h
- **Added `#define SYS_dumppanic 22`**
- **Added `#define SYS_logtest 23`**

### kernel/syscall.c
- **Added `extern uint64 sys_dumppanic(void);`**
- **Added `extern uint64 sys_logtest(void);`**
- **Added `[SYS_dumppanic] sys_dumppanic,`** to the syscalls array
- **Added `[SYS_logtest] sys_logtest,`** to the syscalls array

### kernel/sysproc.c
- **Added `#include "paniclog.h"`** at top
- **Added `sys_dumppanic()`** implementation
- **Added `sys_logtest()`** implementation (calls `log_test()`)

### user/user.h
- **Added `int dumppanic(void*, uint64*);`** declaration
- **Added `int logtest(void);`** declaration

### user/usys.pl
- **Added `entry("dumppanic");`**
- **Added `entry("logtest");`**

### kernel/paniclog.c
- **Added `log_test()`** function — exercises all log levels, buffer wrap, format specifiers, and flush

### Makefile
- **Added `$K/paniclog.o`** to `OBJS`
- **Added `$U/_dumppanic\`** to `UPROGS`
- **Added `$U/_logtest\`** to `UPROGS`

## Summary of Changes by Line Count

| File | Lines Added | Lines Modified | Lines Removed |
|------|-------------|----------------|---------------|
| kernel/paniclog.h | 62 | 0 | 0 |
| kernel/paniclog.c | 295 | 0 | 0 |
| user/dumppanic.c | 90 | 0 | 0 |
| kernel/printf.c | 1 | 15 | 0 |
| kernel/main.c | 1 | 0 | 0 |
| kernel/defs.h | 10 | 0 | 0 |
| kernel/syscall.h | 1 | 0 | 0 |
| kernel/syscall.c | 2 | 0 | 0 |
| kernel/sysproc.c | 1 | 28 | 0 |
| user/user.h | 1 | 0 | 0 |
| user/usys.pl | 1 | 0 | 0 |
| Makefile | 2 | 0 | 0 |
| **Total** | ~467 | 15 | 0 |

## Build Steps

1. Drop new files into `kernel/` and `user/`
2. Apply patches to modified files (or copy the full versions)
3. Run `make clean && make` from the xv6-riscv root
4. Run `make qemu` to boot
5. At the `$` prompt, type `dumppanic` to view any saved crash context
