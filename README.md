# Kernel Panic Logger for xv6-riscv — Complete Explanation

## Table of Contents
1. [Project Overview](#1-project-overview)
2. [Architecture Diagram](#2-architecture-diagram)
3. [Memory Layout](#3-memory-layout)
4. [New Files](#4-new-files)
   - [4a. `kernel/paniclog.h`](#4a-kernelpaniclogh)
   - [4b. `kernel/paniclog.c`](#4b-kernelpaniclogc)
   - [4c. `user/dumppanic.c`](#4c-userdumppanicc)
   - [4d. `user/logtest.c`](#4d-userlogtestc)
5. [Modified Files — Original vs Changed](#5-modified-files--original-vs-changed)
   - [5a. `kernel/printf.c` — panic()](#5a-kernelprintfc--panic)
   - [5b. `kernel/main.c` — boot init](#5b-kernelmainc--boot-init)
   - [5c. `kernel/defs.h` — declarations](#5c-kerneldefsh--declarations)
   - [5d. `kernel/syscall.h` — syscall numbers](#5d-kernelsyscallh--syscall-numbers)
   - [5e. `kernel/syscall.c` — externs + table](#5e-kernelsyscallc--externs--table)
   - [5f. `kernel/sysproc.c` — syscall handlers](#5f-kernelsysprocc--syscall-handlers)
   - [5g. `user/user.h` — user prototypes](#5g-useruserh--user-prototypes)
   - [5h. `user/usys.pl` — assembly stubs](#5h-userusyspl--assembly-stubs)
   - [5i. `Makefile` — build rules](#5i-makefile--build-rules)
6. [Build and Run Instructions](#6-build-and-run-instructions)
7. [Testing](#7-testing)
   - [7a. Expected Output: dumppanic (no panic)](#7a-expected-output-dumppanic-no-panic)
   - [7b. Expected Output: logtest](#7b-expected-output-logtest)
   - [7c. Expected Output: kernel panic](#7c-expected-output-kernel-panic)
8. [Memory Usage and Side Effects](#8-memory-usage-and-side-effects)
9. [Architecture Decision Record](#9-architecture-decision-record)
10. [Known Limitations](#10-known-limitations)

---

## 1. Project Overview

This project adds a **Kernel Panic Logger** to the xv6-riscv teaching operating system. The feature provides:

- **Circular log buffer** (64 entries) — kernel functions can record INFO/WARN/DEBUG/PANIC messages with timestamps, source file, and line number
- **Crash context capture** — when `panic()` is called, the system saves all 32 RISC-V integer registers, 4 supervisor CSRs, current process info, CPU/hart id, and a stack trace (via frame-pointer walk) into a statically allocated `crash_context` struct
- **Log flush** — on panic, the circular buffer is printed to the console (most-recent-first) before the panic message
- **User-space utilities** — `dumppanic` retrieves the saved crash context via a new syscall; `logtest` provides automated testing
- **Automated test suite** — `logtest` runs 5 tests validating the log system, syscall, crash context structure, and stress-tests the circular buffer

**Language:** All C (C99/C11 style), targetting RISC-V 64-bit. Some inline assembly for register capture.

---

## 2. Architecture Diagram

```
+--------------------------------------------------------------------+
|                        USER SPACE                                   |
|                                                                     |
|  dumppanic  (calls sys_dumppanic, receives crash_context)          |
|       |                                                             |
|  logtest    (calls sys_logtest, exercises kernel log system)       |
|       |                                                             |
|       | syscall                                                     |
|       v                                                             |
+--------------------------------------------------------------------+
|                        KERNEL SPACE                                 |
|                                                                     |
|  +--------------------------------------------------------------+  |
|  |                    panic() (printf.c)                         |  |
|  |  1. log_save_crash_context(msg)   ← capture live state       |  |
|  |  2. log_flush()                   ← print pending log msgs   |  |
|  |  3. printf("panic: ...")          ← original behavior        |  |
|  |  4. log_dump_crash_context()      ← print register dump      |  |
|  |  5. infinite loop                                           |  |
|  +--------------------------------------------------------------+  |
|                                                                     |
|  +-------------------+    +--------------------------------------+  |
|  |  log_info()       |--->|  Circular Log Buffer                 |  |
|  |  log_warn()       |    |  (64 entries, spinlock-protected)    |  |
|  |  log_debug()      |    |  Each entry: ticks, level, file,     |  |
|  |  log_panic_prep() |    |  line, msg[128]                      |  |
|  +-------------------+    +--------------------------------------+  |
|                                                                     |
|  +--------------------------------------------------------------+  |
|  |  saved_crash_ctx (static struct crash_context)                |  |
|  |  - RISC-V registers (32 int regs + 4 CSRs)                  |  |
|  |  - Process info (pid, name, state)                           |  |
|  |  - Stack trace (frame pointer walk, up to 10 frames)         |  |
|  |  - CPU/hart, timestamp, panic message                        |  |
|  |  - magic=0xDEADBEEF validation                               |  |
|  +--------------------------------------------------------------+  |
|                                                                     |
|  +--------------------------------------------------------------+  |
|  |  sys_dumppanic()  (sysproc.c)                                |  |
|  |  copyout(saved_crash_ctx -> user buffer)                     |  |
|  +--------------------------------------------------------------+  |
+--------------------------------------------------------------------+
```

**Data flow:**
1. Kernel code calls `log_info()` / `log_warn()` / `log_debug()` → entries stored in `logbuf`
2. A fatal error calls `panic("message")`
3. `panic()` calls `log_save_crash_context()` → inline asm captures registers → CSR reads → process info → stack walk
4. `panic()` calls `log_flush()` → circular buffer printed to UART (most-recent-first)
5. `panic()` prints original `panic: message`
6. `panic()` calls `log_dump_crash_context()` → full register/CSR/stack dump printed
7. User can later run `dumppanic` → `sys_dumppanic()` → `copyout(saved_crash_ctx)` → formatted display

---

## 3. Memory Layout

```
Physical Memory (xv6-riscv / QEMU riscv-virt)
==============================================

0x00001000  boot ROM (QEMU)
0x02000000  CLINT
0x0C000000  PLIC
0x10000000  UART0
0x10001000  VIRTIO disk
...
0x80000000  KERNBASE — kernel text, data, BSS
    |
    |   [kernel text]
    |   [kernel data]
    |   [kernel BSS]
    |       - paniclog.c: logbuf (circular buffer)   ← static BSS
    |       - paniclog.c: saved_crash_ctx            ← static BSS
    |   end (start of free pages)
    |
    |   [free pages managed by kalloc]
    |
0x88000000  PHYSTOP (KERNBASE + 128 MB)

Conceptual "Reserved Region" (documented in paniclog.h):
  0x6000  — CRASH_CONTEXT_RESERVED_BASE (conceptual marker)
  0x6400  — LOG_BUF_RESERVED_BASE       (conceptual marker)
  In xv6-riscv these addresses fall below KERNBASE and are
  NOT directly writable.  Instead, the log buffer and crash
  context live in kernel BSS (statically allocated).  The
  reserved-base constants are provided for documentation
  compatibility with the design specification.
```

---

## 4. New Files

### 4a. `kernel/paniclog.h`

**Purpose:** Public header defining data structures, constants, and externs for the logging subsystem.

**Key definitions:**

| Symbol | Value | Description |
|--------|-------|-------------|
| `LOG_INFO` | 0 | Informational log level |
| `LOG_WARN` | 1 | Warning log level |
| `LOG_PANIC` | 2 | Panic-level log entry |
| `LOG_DEBUG` | 3 | Debug log level |
| `LOG_SIZE` | 64 | Circular buffer capacity (entries) |
| `LOG_MSG_MAX` | 128 | Max formatted message length |
| `LOG_FILE_MAX` | 32 | Max source file name length |
| `CRASH_MAGIC` | 0xDEADBEEF | Magic number validating crash_context |

**`struct log_entry`** — single circular buffer slot:
```c
struct log_entry {
  uint      ticks;       // CPU ticks at log time
  int       level;       // LOG_INFO, LOG_WARN, LOG_PANIC, LOG_DEBUG
  char      file[32];    // source file name (basename)
  int       line;        // source line number
  char      msg[128];    // formatted message
};
```

**`struct crash_context`** — full machine state snapshot at panic (~592 bytes):
```c
struct crash_context {
  // 32 RISC-V integer registers
  uint64 ra, sp, gp, tp;
  uint64 t0, t1, t2;
  uint64 s0, s1;
  uint64 a0, a1, a2, a3, a4, a5, a6, a7;
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  uint64 t3, t4, t5, t6;
  // 4 supervisor CSRs
  uint64 sepc, scause, stval, sstatus;
  // Process info
  int    pid;
  char   pname[16];
  int    pstate;
  // Stack trace
  uint64 stacktrace[10];
  int    stack_depth;
  // CPU and timestamp
  int    cpu;
  char   panic_msg[128];
  uint64 panic_ticks;
  // Validation
  uint32 magic;
};
```

**Extern:** `extern struct crash_context saved_crash_ctx;` — allows `sysproc.c` to copy it to user space.

**Design rationale:** Separating type definitions into their own header allows both kernel code (`paniclog.c`, `printf.c`, `sysproc.c`) and user code (`dumppanic.c`) to share the same struct definitions. The `-I.` compiler flag in the Makefile lets user programs include kernel headers.

---

### 4b. `kernel/paniclog.c`

**Purpose:** Implements the entire logging subsystem — circular buffer, public logging API, crash context capture, panic dump, initialization, and self-test.

**Complete function table:**

| Function | Visibility | Lines | Description |
|----------|-----------|-------|-------------|
| `lvlstr()` | `static` | 39-48 | Converts log level integer to string ("INFO", "WARN", "PANIC", "DEBUG") |
| `vsnprintf()` | `static` | 53-144 | Minimal `vsnprintf` supporting `%d %ld %lld %u %lu %llu %x %lx %llx %p %s %c %%` |
| `log_add()` | `static` | 149-174 | Thread-safe circular buffer insertion (acquires `logbuf.lock`) |
| `log_info()` | public | 179-186 | Public API: log at INFO level |
| `log_warn()` | public | 191-198 | Public API: log at WARN level |
| `log_debug()` | public | 203-210 | Public API: log at DEBUG level |
| `log_panic_prep()` | public | 215-222 | Public API: log at PANIC level (called just before panic) |
| `log_flush()` | public | 227-248 | Dumps all buffered entries (most-recent-first) to console |
| `log_save_crash_context()` | public | 253-324 | Captures full CPU/process state at panic |
| `log_dump_crash_context()` | public | 329-386 | Prints saved crash context to console |
| `log_init()` | public | 391-400 | Initializes lock, clears buffer and context |
| `log_test()` | public | 405-429 | Self-test exercising all log functions, wrap, format specifiers |

**Design rationale for key decisions:**

- **Circular buffer:** Fixed-size (64 entries). Oldest entries are overwritten first. No dynamic allocation — avoids memory exhaustion in kernel context.
- **Spinlock:** `logbuf.lock` protects the circular buffer from concurrent access by multiple CPUs. Standard xv6 concurrency primitive.
- **Custom vsnprintf:** Written from scratch rather than reusing `printf.c`'s `consputc`-based output. The log buffer needs to format into a string buffer, not to the UART. Supports the same format specifiers as the existing `printf()`.
- **Register capture via inline asm:** Each RISC-V register is read with `asm volatile("mv %0, <reg>" : "=r"(field))`. This is the only way to read general-purpose registers in C — there are no CSR-like read functions for GPRs.
- **CSR reads:** Uses existing inline functions from `riscv.h`: `r_sepc()`, `r_scause()`, `r_stval()`, `r_sstatus()`.
- **Stack walk:** RISC-V ABI stores frame records as `[saved_s0, saved_ra]` at each stack frame. Walking the `s0` (frame pointer) chain gives return addresses without requiring DWARF unwind tables. Bounded to 10 frames.
- **Most-recent-first flush:** When a panic occurs, the most recent entries (closest to the fault) are most relevant for debugging.

---

### 4c. `user/dumppanic.c`

**Purpose:** User-space utility that calls the `dumppanic()` syscall to retrieve the saved `crash_context` and prints it in a human-readable format.

**Functions:**

| Function | Description |
|----------|-------------|
| `print_ctx(ctx)` | Formats and prints the crash context: magic validation → message → registers → CSRs → process info → stack trace |
| `main(argc, argv)` | Allocates `crash_context` on stack, calls `dumppanic()`, validates size, calls `print_ctx()` |

**Output format (when a panic has occurred):**
```
========== SAVED KERNEL PANIC CRASH DUMP ==========
Message: sched locks
Ticks:   1234
CPU:     0

-- RISC-V Register State --
ra=0x8000xxxx  sp=0x8000xxxx  gp=0x8000xxxx  tp=0x00000000
...

-- Supervisor CSRs --
sepc=0x8000xxxx  scause=0x00000000  stval=0x00000000  sstatus=0x00000020

-- Process Info --
pid=2  name=sh  state=4

-- Stack Trace (FP walk, 3 frames) --
  [0] 0x8000xxxx
  [1] 0x8000xxxx
  [2] 0x8000xxxx
====================================================
```

**Output format (no panic yet):**
```
========== SAVED KERNEL PANIC CRASH DUMP ==========
(No valid crash context found. magic=0x0, expected 0xdeadbeef)
```

---

### 4d. `user/logtest.c`

**Purpose:** Automated test suite for the kernel panic log system. Runs 5 tests and reports PASS/FAIL for each.

**Tests performed:**

| # | Test Name | What it validates |
|---|-----------|-------------------|
| 1 | `kernel log_test` | Calls `logtest()` syscall which exercises all log levels, circular buffer wrap (69 entries), format specifier coverage, and `log_flush()` |
| 2 | `dumppanic syscall` | Verifies `dumppanic()` returns 0 and size matches `sizeof(crash_context)` |
| 3 | `magic is 0 before panic` | Ensures `crash_context.magic == 0` when no panic has occurred |
| 4 | `fields zeroed before panic` | Ensures registers and process fields are zero at fresh boot |
| 5 | `repeated log_test (5x stress)` | Runs kernel log test 5 times to detect buffer corruption or lock issues |

---

## 5. Modified Files — Original vs Changed

For each modified file, this section shows the **original xv6-riscv code** side by side with the **changed version** and explains why each change was made.

---

### 5a. `kernel/printf.c` — `panic()`

**What changed:** Added `#include "paniclog.h"` at the top and replaced the entire `panic()` function body.

**Original xv6 `panic()`:**
```c
void
panic(char *s)
{
  panicking = 1;
  printf("panic: ");
  printf("%s\n", s);
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}
```

**Changed `panic()`:**
```c
void
panic(char *s)
{
  panicking = 1;

  log_save_crash_context(s);
  log_flush();

  printf("panic: ");
  printf("%s\n", s);
  log_dump_crash_context();

  panicked = 1;
  for(;;)
    ;
}
```

**Why this order matters:**
1. `log_save_crash_context(s)` is called **first** — must capture live register values before any function calls or console output modify them
2. `log_flush()` dumps all queued log entries (most-recent-first) so developers can see what led up to the crash
3. Original `printf("panic: ...")` is preserved for backward compatibility
4. `log_dump_crash_context()` prints the full register/CSR/stack/process dump for immediate debugging
5. `panicked = 1` prevents other CPUs from printing via UART (pre-existing xv6 behavior)

Also added `#include "paniclog.h"` to the includes section (line 17).

---

### 5b. `kernel/main.c` — boot init

**What changed:** Added a single call to `log_init()` early in the CPU 0 boot path.

**Original xv6 `main()` (CPU 0 block):**
```c
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();
    ...
```

**Changed `main()` (CPU 0 block):**
```c
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    log_init();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();
    ...
```

**Why:** The log buffer's spinlock (`logbuf.lock`) must be initialized via `initlock()` before any code calls `log_info()`, `log_warn()`, etc. Placing `log_init()` right after `printfinit()` is the earliest safe point — console and printf infrastructure are already set up, so any logging during boot will be visible if needed. No other CPU initialization code needs changing.

---

### 5c. `kernel/defs.h` — declarations

**What changed:** Added 9 function declarations for the paniclog module between the `log.c` and `pipe.c` sections.

**Original xv6** (between log.c and pipe.c):
```c
// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// pipe.c
```

**Changed:**
```c
// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// paniclog.c
void            log_init(void);
void            log_info(const char*, int, const char*, ...);
void            log_warn(const char*, int, const char*, ...);
void            log_debug(const char*, int, const char*, ...);
void            log_panic_prep(const char*, int, const char*, ...);
void            log_flush(void);
void            log_save_crash_context(const char*);
void            log_dump_crash_context(void);
int             log_test(void);

// pipe.c
```

**Why:** xv6 uses a centralized declaration file (`defs.h`) that every kernel C file includes. Adding declarations here makes all paniclog functions available to any kernel file (e.g., `printf.c` calls `log_save_crash_context`, `log_flush`, `log_dump_crash_context`; `sysproc.c` calls `log_test`). This follows the existing xv6 convention.

---

### 5d. `kernel/syscall.h` — syscall numbers

**What changed:** Added two new syscall numbers at the end.

**Original xv6:**
```c
#define SYS_fork    1
#define SYS_exit    2
...
#define SYS_close  21
```

**Changed:**
```c
#define SYS_fork    1
#define SYS_exit    2
...
#define SYS_close  21
#define SYS_dumppanic 22
#define SYS_logtest   23
```

**Why:** Each user-to-kernel service in xv6 is identified by a syscall number. We need two new services: `dumppanic` to read the crash context, and `logtest` to exercise the log system. Numbers 22 and 23 are the next available after the existing 21 (`SYS_close`).

---

### 5e. `kernel/syscall.c` — externs + table

**What changed:** Added `extern` declarations and syscall table entries for the two new syscalls.

**Original xv6** (externs section):
```c
extern uint64 sys_close(void);
```

**Changed (externs):**
```c
extern uint64 sys_close(void);
extern uint64 sys_dumppanic(void);
extern uint64 sys_logtest(void);
```

**Original xv6** (syscalls[] table):
```c
[SYS_close]   sys_close,
```

**Changed (table):**
```c
[SYS_close]   sys_close,
[SYS_dumppanic] sys_dumppanic,
[SYS_logtest]   sys_logtest,
```

**Why:** The `syscalls[]` array maps syscall numbers to handler functions. Every new syscall needs both an `extern` declaration and a table entry. The array uses designated initializers (`[SYS_xxx]`) so order doesn't matter.

---

### 5f. `kernel/sysproc.c` — syscall handlers

**What changed:** Added `#include "paniclog.h"` and two new function implementations after `sys_uptime()`.

**Original xv6** (after `sys_uptime()`): End of file.

**Changed** (added at end of file):
```c
// #include "paniclog.h" also added to includes

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

uint64
sys_logtest(void)
{
  return log_test();
}
```

**Why:**
- `sys_dumppanic()` takes two arguments from user space via `argaddr()`: a destination buffer address and a pointer to write the size. It writes `sizeof(saved_crash_ctx)` to the size pointer, then copies the entire `crash_context` struct to user space via `copyout()`. This follows the same pattern as other xv6 syscalls (e.g., `sys_fstat` copies a `stat` struct to user space).
- `sys_logtest()` simply calls the kernel's `log_test()` function and returns its result. This provides a way for user-space programs to exercise the kernel log system without triggering a panic.
- `#include "paniclog.h"` is needed for access to `saved_crash_ctx` and `CRASH_MAGIC`.

---

### 5g. `user/user.h` — user prototypes

**What changed:** Added forward declaration of `struct crash_context` and function prototypes for `dumppanic()` and `logtest()`.

**Original xv6:**
```c
struct stat;

// system calls
int fork(void);
...
int uptime(void);

// ulib.c
```

**Changed:**
```c
struct stat;
struct crash_context;

// system calls
int fork(void);
...
int uptime(void);
int dumppanic(void*, uint64*);
int logtest(void);

// ulib.c
```

**Why:** User-space programs that call `dumppanic()` and `logtest()` need prototypes for the compiler. The `struct crash_context` forward declaration allows the `dumppanic()` prototype to use a pointer to it without requiring the full struct definition (which comes from `kernel/paniclog.h` included by the user program).

---

### 5h. `user/usys.pl` — assembly stubs

**What changed:** Added two `entry()` calls at the end of the Perl script that generates `usys.S`.

**Original xv6:**
```perl
entry("uptime");
```

**Changed:**
```perl
entry("uptime");
entry("dumppanic");
entry("logtest");
```

**Why:** `entry()` generates a small assembly stub for each syscall (loads the syscall number into register `a7`, executes `ecall`, and returns). Without these stubs, user programs cannot make the syscalls. The generated code for each entry looks like:
```asm
.global dumppanic
dumppanic:
 li a7, SYS_dumppanic
 ecall
 ret
```

---

### 5i. `Makefile` — build rules

**What changed:** Added `$K/paniclog.o` to `OBJS` and `$U/_dumppanic` + `$U/_logtest` to `UPROGS`.

**Original xv6 OBJS (end):**
```makefile
  $K/virtio_disk.o
```

**Changed OBJS:**
```makefile
  $K/virtio_disk.o \
  $K/paniclog.o
```

**Original xv6 UPROGS (end):**
```makefile
	$U/_zombie\
```

**Changed UPROGS:**
```makefile
	$U/_zombie\
	$U/_dumppanic\
	$U/_logtest\
```

**Why:**
- `OBJS` lists all object files that are linked into `kernel/kernel`. Adding `$K/paniclog.o` ensures the paniclog module is compiled and linked.
- `UPROGS` lists all user-space binaries that are packed into `fs.img`. Adding `_dumppanic` and `_logtest` makes them available at the shell prompt.
- The `_` prefix is xv6's convention: the Makefile builds `$U/_dumppanic` from `$U/dumppanic.o` using the implicit rule `_%: %.o $(ULIB) $U/user.ld`.

---

## 6. Build and Run Instructions

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y git build-essential gdb-multiarch \
  binutils-riscv64-unknown-elf gcc-riscv64-unknown-elf \
  qemu-system-misc perl bc
```

This installs:
- `riscv64-unknown-elf-gcc` — RISC-V cross-compiler
- `qemu-system-riscv64` — RISC-V emulator
- `make` — build system
- `perl` — generates syscall stubs
- `bc` — version comparison in Makefile

### Build

```bash
cd xv6-riscv
make clean
make
```

**Build output files:**

| File | Description |
|------|-------------|
| `kernel/kernel` | RISC-V ELF kernel binary (loaded by QEMU at `0x80000000`) |
| `kernel/kernel.asm` | Full kernel disassembly |
| `kernel/kernel.sym` | Kernel symbol table |
| `fs.img` | Filesystem image with all user programs |
| `kernel/*.o` | Compiled kernel object files |
| `user/*.o` | Compiled user program objects |

### Run in QEMU

```bash
make qemu
```

QEMU starts with 3 CPUs, 128 MB RAM, a virtio disk, and a serial console. You'll see boot messages and then a `$` prompt.

### Exit QEMU

Press `Ctrl+A` then `X`.

---

## 7. Testing

### 7a. Expected Output: `dumppanic` (no panic yet)

```
$ dumppanic

========== SAVED KERNEL PANIC CRASH DUMP ==========
(No valid crash context found. magic=0x0, expected 0xdeadbeef)
```

This confirms the syscall works but `saved_crash_ctx.magic` is 0 because no panic has occurred.

### 7b. Expected Output: `logtest`

```
$ logtest
logtest: starting kernel panic log tests
  TEST 1: kernel log_test ... PASS
  TEST 2: dumppanic syscall ... PASS
  TEST 3: magic is 0 before panic ... PASS
  TEST 4: fields zeroed before panic ... PASS
  TEST 5: repeated log_test (5x stress) ... PASS

logtest: 5/5 tests passed
logtest: ALL TESTS PASSED
```

The kernel `log_flush()` output also appears during test 1:
```
--- LOG FLUSH (most recent first) ---
[   42][PANIC] paniclog.c:413 log_test: PANIC prep (num=-1)
...
--- END LOG FLUSH ---
```

### 7c. Expected Output: kernel panic

To trigger a panic, temporarily add `panic("test panic");` after `log_init();` in `main.c:16`, rebuild, and run:

```
--- LOG FLUSH (most recent first) ---
--- END LOG FLUSH ---
panic: test panic

========== KERNEL PANIC CRASH DUMP ==========
Message: test panic
Ticks:   42
CPU:     0

-- RISC-V Register State --
ra=0x8000xxxx  sp=0x8000xxxx  gp=0x8000xxxx  tp=0x00000000
s0=0x8000xxxx  s1=0x00000000
a0=0x8000xxxx  a1=0x8000xxxx  a2=0x00000000  a3=0x00000000
...

-- Supervisor CSRs --
sepc=0x8000xxxx  scause=0x00000000  stval=0x00000000  sstatus=0x00000020

-- Process Info --
pid=-1  name=(none)  state=-1

-- Stack Trace (FP walk, N frames) --
  [0] 0x8000xxxx
  [1] 0x8000xxxx
...

============================================
```

Register and address values will vary — the key is that registers are non-zero and the stack trace contains valid kernel addresses.

---

## 8. Memory Usage and Side Effects

| Resource | Usage |
|----------|-------|
| **Log buffer (static BSS)** | `64 × (4+4+32+4+128) = 64 × 172 ≈ 11 KB` |
| **Crash context (static BSS)** | `≈ 592 bytes` |
| **vsnprintf stack buffer** | `24 bytes per call` (temporary digit buffer) |
| **Total additional memory** | **≈ 12 KB** — all statically allocated, no heap/free-page usage |

**Spinlock contention:** `logbuf.lock` is acquired on every `log_*()` call and on `log_flush()`. In normal operation these calls are infrequent. During panic, the lock is uncontended because only one CPU panics.

**Stack usage:** `log_save_crash_context()` does not use recursion. The frame-pointer walk is bounded to 10 iterations. Total stack usage per call is well under 200 bytes.

**UART freeze:** When `panicked=1`, UART output is frozen for other CPUs. This is pre-existing behavior in xv6 (the UART's `uartputc_sync()` spins when `panicked` is set) and is unchanged by our modifications.

**Compiler compatibility:** The code requires `-fno-omit-frame-pointer` (already in CFLAGS) for the frame-pointer stack walk to work correctly.

---

## 9. Architecture Decision Record

| Decision | Rationale |
|----------|-----------|
| **Static BSS vs. fixed physical address** | Fixed physical addresses below KERNBASE (0x6000-0x7000) conflict with QEMU's boot ROM and CLINT. BSS is simpler, safer, and functionally equivalent. |
| **Spinlock for log buffer** | Standard xv6 concurrency primitive. Adequate for infrequent logging calls. No need for a more complex lock-free structure. |
| **Custom vsnprintf vs. reusing printf** | `printf()` writes directly to UART via `consputc()`. The log buffer needs formatted string *storage*, not console output. A custom `vsnprintf` is the cleanest approach. |
| **RISC-V registers (not x86)** | Codebase is xv6-riscv port. x86 register names do not exist on this architecture. |
| **Frame-pointer stack walk vs. DWARF** | FP walk is simple (~10 lines), fast, and requires no external data. DWARF unwind tables are large and complex. |
| **Most-recent-first log flush** | When debugging a panic, the most recent entries (closest to the fault) are most relevant. |
| **Separate syscall vs. /dev-style interface** | xv6 has no device file system for kernel memory access. A syscall follows the existing pattern (e.g., `uptime` syscall, `fstat` syscall). |
| **32 general-purpose registers captured** | RISC-V has 32 integer registers x0–x31 (x0 is hardwired to 0, so 31 are captured). Complete state dump is essential for post-mortem debugging. |
| **4 supervisor CSRs captured** | `sepc` (fault PC), `scause` (fault cause), `stval` (fault address), `sstatus` (interrupt state) — these are the minimum needed to understand a trap/panic. |
| **10-frame stack trace** | Bounded walk prevents infinite loops from corrupted frame pointers. 10 frames is sufficient for most kernel panic scenarios. |
| **`log_test()` as kernel function + syscall** | The kernel function exercises the log system directly. The syscall wrapper allows user-space test programs to call it. This separation keeps test logic in user space while execution stays in kernel context. |

---



7. **Recursion safety:** If `printf()` inside `log_dump_crash_context()` triggers another panic (unlikely), infinite recursion would occur. The `panicking` guard in `printf()` mitigates this by skipping lock acquisition, but a re-entrant panic would still loop.

8. **No SMP panic coordination:** If multiple CPUs panic simultaneously, the first to set `panicking=1` wins. Other CPUs' crash contexts are lost. This is identical to original xv6 behavior.
