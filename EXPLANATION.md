# Kernel Panic Logger for xv6-riscv — EXPLANATION

## 1. Overall Architecture

```
+--------------------------------------------------------------------+
|                        USER SPACE                                   |
|  dumppanic  (calls sys_dumppanic, receives crash_context)          |
|       |                                                             |
|       | syscall                                                     |
|       v                                                             |
+--------------------------------------------------------------------+
|                        KERNEL SPACE                                 |
|                                                                     |
|  +--------------------------------------------------------------+  |
|  |                    panic() (printf.c)                         |  |
|  |  1. log_save_crash_context(msg)                              |  |
|  |  2. log_flush()                                              |  |
|  |  3. printf("panic: ...")                                     |  |
|  |  4. log_dump_crash_context()                                 |  |
|  |  5. infinite loop                                            |  |
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

## 2. Memory Layout

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

## 3. Changed Files — Detailed Rationale

### 3a. `kernel/paniclog.h` (NEW)
**Purpose:** Define the public data structures and constants for the logging subsystem.

**What it contains:**
- Log level enums (`LOG_INFO`, `LOG_WARN`, `LOG_PANIC`, `LOG_DEBUG`)
- `struct log_entry` — a single circular-buffer slot holding tick count, level, source file/line, and formatted message string
- `struct crash_context` — the full machine state snapshot taken at panic. Adapted for RISC-V 64-bit:
  - 32 integer registers (ra, sp, gp, tp, t0–t6, s0–s11, a0–a7)
  - 4 supervisor CSRs (sepc, scause, stval, sstatus)
  - process info (pid, name, procstate)
  - stack trace array (10 return addresses from frame-pointer walk)
  - CPU id, panic message, timestamp, magic marker
- `CRASH_MAGIC` constant (0xDEADBEEF) used to validate that saved context is meaningful

**Design Rationale:** Separating the type definitions into their own header keeps the implementation clean and allows both kernel code (`paniclog.c`, `printf.c`, `sysproc.c`) and user code (`dumppanic.c`) to share the same struct definitions. The user program includes this header with `-I.` via the Makefile's CFLAGS.

**Memory:** Static definitions only; no dynamic allocation. `sizeof(struct crash_context)` ≈ 592 bytes.

---

### 3b. `kernel/paniclog.c` (NEW)
**Purpose:** Implements all logging functionality.

**Key components:**

| Function | Lines | Purpose |
|----------|-------|---------|
| `vsnprintf()` | 65–145 | Internal printf-to-buffer formatter supporting `%d %ld %lld %u %lu %llu %x %lx %llx %p %s %c %%` |
| `log_add()` | 151–175 | Thread-safe circular buffer insertion (acquires spinlock) |
| `log_info()` | 179–184 | Public API: log at INFO level |
| `log_warn()` | 186–191 | Public API: log at WARN level |
| `log_debug()` | 193–198 | Public API: log at DEBUG level |
| `log_panic_prep()` | 202–208 | Public API: log at PANIC level (called just before panic) |
| `log_flush()` | 212–231 | Dumps all buffered entries (most-recent-first) to console |
| `log_save_crash_context()` | 235–281 | Captures full CPU/process state at panic |
| `log_dump_crash_context()` | 285–335 | Prints saved crash context to console |
| `log_init()` | 339–346 | Initializes lock, clears buffer and context |

**Design Rationale:**
- **Circular buffer:** Fixed-size (64 entries) to avoid memory exhaustion. Oldest entries are overwritten first.
- **Spinlock:** `logbuf.lock` protects the circular buffer from concurrent access by multiple CPUs.
- **vsnprintf:** Written from scratch (not reusing printf.c's `consputc`-based output) so that log messages can be formatted into a string buffer without going through the UART.
- **Stack walk:** RISC-V ABI stores frame records as `[saved_s0, saved_ra]`. Walking the `s0` (frame pointer) chain gives us return addresses without requiring DWARF unwind tables.
- **Register capture:** Uses inline assembly `mv` instructions to read RISC-V general-purpose registers. CSR registers are read via the existing `r_sepc()`, `r_scause()`, `r_stval()`, `r_sstatus()` inline functions from `riscv.h`.

**Concurrency:** The spinlock ensures mutual exclusion. However, `log_save_crash_context()` is called from `panic()` after `panicking=1`, which means `printf()` no longer acquires `pr.lock` (see `printf.c` line 70-71). The `logbuf.lock` is still acquired inside `log_flush()`, but since only one CPU can be in panic at a time (the panic freezes others via `panicked=1`), this is safe.

---

### 3c. `kernel/printf.c` — `panic()` modification

**Original code:**
```c
void panic(char *s) {
  panicking = 1;
  printf("panic: ");
  printf("%s\n", s);
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}
```

**Modified code:**
```c
void panic(char *s) {
  panicking = 1;
  log_save_crash_context(s);    // 1. Capture state before any output
  log_flush();                   // 2. Flush pending log entries
  printf("panic: ");             // 3. Original output
  printf("%s\n", s);
  log_dump_crash_context();      // 4. Dump captured context
  panicked = 1;                  // 5. Freeze other CPUs
  for(;;)                        // 6. Infinite loop
    ;
}
```

**Why this order matters:**
1. `log_save_crash_context()` is called **first** because it captures live register state. Once we start printing or modifying state, register values may change.
2. `log_flush()` dumps queued log messages so that the last N kernel messages leading up to the panic are visible.
3. The original `printf("panic: ...")` is preserved for backward compatibility.
4. `log_dump_crash_context()` prints the full register/stack/process dump for immediate debugging.
5. `panicked = 1` prevents other CPUs from printing via UART.
6. Infinite loop halts execution.

---

### 3d. `kernel/main.c` — `log_init()` call

**Change:** Added `log_init();` immediately after `printfinit();` on the CPU 0 boot path.

**Why:** The log buffer and its spinlock must be initialized before any code that calls `log_info()`, `log_warn()`, etc. This is the earliest safe point — console and printf are already set up, so any logging during boot will be visible.

---

### 3e. `kernel/defs.h` — Function declarations

**Change:** Added 7 function declarations for the paniclog module. These are the public API that other kernel files (e.g., `printf.c`) need to call.

**Why:** Centralized declarations follow xv6 convention. Every kernel C file includes `defs.h`, making the logging functions universally available.

---

### 3f. Syscall additions (syscall.h, syscall.c, sysproc.c)

**Why:** The `dumppanic` user-space utility needs a way to read the saved crash context. A system call is the standard xv6 mechanism for user-to-kernel communication.

**sys_dumppanic() design:**
- Takes two arguments: a user-space destination buffer address, and a pointer to write the size
- Returns `sizeof(crash_context)` to the user via the size pointer
- Copies the static `saved_crash_ctx` struct to user space via `copyout()`
- Uses `safestrcpy`/direct struct copy, no dynamic allocation needed

---

### 3g. User-space changes (user.h, usys.pl, dumppanic.c)

**Why:**
- `user.h` — declares the `dumppanic()` syscall wrapper so user programs can call it
- `usys.pl` — generates the assembly stub (`ecall` instruction) that traps into the kernel
- `dumppanic.c` — the user utility that calls the syscall, validates the returned data, and formats a readable dump to stdout

**dumppanic.c output format:**
```
========== SAVED KERNEL PANIC CRASH DUMP ==========
Message: sched locks
Ticks:   1234  CPU: 0
ra=0x... sp=0x... gp=0x... tp=0x...
...
Stack trace:
  [0] 0x...
  [1] 0x...
```

---

### 3h. Makefile

**Changes:**
- Added `$K/paniclog.o` to `OBJS` so the kernel is linked with the log module
- Added `$U/_dumppanic\` to `UPROGS` so the `dumppanic` binary is included in the filesystem image

---

## 4. Potential Side Effects / Memory Usage

| Resource | Usage |
|----------|-------|
| **Log buffer (static BSS)** | `64 × (4+4+32+4+128) = 64 × 172 ≈ 11 KB` |
| **Crash context (static BSS)** | `≈ 592 bytes` |
| **vsnprintf stack buffer** | `24 bytes per call` (temporary digit buffer) |
| **Total** | **≈ 12 KB** — all statically allocated, no heap/free-page usage |

**Spinlock contention:** The `logbuf.lock` is acquired on every `log_*()` call and on `log_flush()`. In normal operation these calls are infrequent. During panic, the lock is uncontended because only one CPU panics.

**Stack usage:** `log_save_crash_context()` does not use deep recursion. The frame-pointer walk is bounded to 10 iterations.

**UART freeze:** When `panicked=1`, the UART output is frozen for other CPUs. This is pre-existing behavior in xv6 and is unchanged.

## 5. How to Test

### Test 1: Basic Boot and Logging
1. Build and boot: `make clean && make && make qemu`
2. Observe startup messages: you should see `log_init()` complete silently (no output needed)
3. At the `$` shell prompt, run `dumppanic`

**Expected output:**
```
$ dumppanic
========== SAVED KERNEL PANIC CRASH DUMP ==========
(No valid crash context found. magic=0x0, expected 0xdeadbeef)
```
This confirms the syscall works but no panic has occurred yet.

### Test 2: Trigger a Panic
1. Add a call to `panic("test panic")` somewhere early in boot (e.g., in `main.c` after `log_init()`)
2. Rebuild and boot

**Expected output (approximately):**
```
--- LOG FLUSH (most recent first) ---
--- END LOG FLUSH ---
panic: test panic

========== KERNEL PANIC CRASH DUMP ==========
Message: test panic
Ticks:   42
CPU:     0
ra=0x8000xxxx sp=0x8000xxxx gp=0x8000xxxx tp=0x00000000
s0=0x8000xxxx  s1=0x00000000
a0=0x8000xxxx  a1=0x8000xxxx  a2=0x00000000  a3=0x00000000
...
-- Supervisor CSRs --
sepc=0x8000xxxx  scause=0x00000000  stval=0x00000000  sstatus=0x00000020
...
-- Process Info --
pid=-1  name=(none)  state=-1
...
-- Stack Trace (FP walk, N frames) --
  [0] 0x8000xxxx
  [1] 0x8000xxxx
...
============================================
```
(Values will vary; the key is that registers are non-zero and the stack trace contains valid kernel addresses.)

### Test 3: Log Buffer with INFO/WARN/DEBUG
1. Add the following code to `main.c` after `log_init()`:
```c
log_info(__FILE__, __LINE__, "booting xv6-riscv");
log_warn(__FILE__, __LINE__, "CPU count: %d", NCPU);
log_debug(__FILE__, __LINE__, "kernel at 0x%lx", KERNBASE);
```
2. Then force a panic as in Test 2

**Expected output:** The "LOG FLUSH" section should show three entries:
```
--- LOG FLUSH (most recent first) ---
[   42][PANIC] main.c:119 panic: test panic
[    5][DEBUG] main.c:25 kernel at 0x80000000
[    3][WARN] main.c:24 CPU count: 8
[    0][INFO] main.c:23 booting xv6-riscv
--- END LOG FLUSH ---
```

### Test 4: Post-Panic dump_panic
- After a panic and reboot (or in a system that does not reset the BSS), run `dumppanic`

**Expected:** The full crash context from the previous panic, with `magic=0xDEADBEEF`.

*Note:* xv6 in QEMU resets all memory on reboot. To test persistence, you would need:
- A non-resetting emulator
- Or leave the QEMU session running and use the debug console
- Or store the crash context in a known memory region that survives warm reboot (on real hardware)

### Test 5: Concurrent Access
- Run multiple processes that each log messages. The spinlock ensures no corruption.
- For a stress test, add `log_info(__FILE__, __LINE__, "heartbeat %d", i);` inside a long loop in a user process.

### Test 6: Manual syscall test
```c
// In a user program:
struct crash_context ctx;
uint64 sz;
int ret = dumppanic(&ctx, &sz);
printf("ret=%d sz=%ld magic=0x%x\n", ret, sz, ctx.magic);
```
- Before any panic: `sz=592, magic=0x0`
- After panic (without reboot): `sz=592, magic=0xDEADBEEF`

## 6. Known Limitations

1. **No persistence across QEMU reboot:** The static BSS variables are zeroed at each boot. The crash context survives only until the next reboot. In a real hardware scenario, the static data would persist across a warm reset (but not cold power-off).

2. **Conceptual reserved memory:** The specification mentions `0x6000–0x7000` as a reserved memory region. On xv6-riscv, this physical range is occupied by QEMU's boot ROM and CLINT. The implementation uses kernel BSS instead, which is functionally equivalent but lives at a different address. The constants `CRASH_CONTEXT_RESERVED_BASE` and `LOG_BUF_RESERVED_BASE` are provided for documentation compatibility.

3. **No virtual-disk write:** The specification mentions "Attempt to write panic data to virtual disk." This is not implemented because xv6-riscv's filesystem (`virtio_disk.c`) depends on interrupts and locks that may not function correctly during panic. Implementing a synchronous panic-write would require a special "panic mode" in the disk driver, which adds complexity and risk.

4. **Stack trace accuracy:** The frame-pointer walk may be truncated or miss frames if the compiler omits frame pointers (`-fno-omit-frame-pointer` is in CFLAGS, so this should work). Functions called before `panic()` establishes its own frame may not appear. The trace gives approximate return addresses but not symbol names (xv6 has no kernel symbol table in the running kernel).

5. **No log rotation or compression:** The circular buffer is fixed at 64 entries. Under heavy logging, older entries are silently overwritten.

6. **No `vsnprintf` in standard library:** The included `vsnprintf` is a minimal implementation that supports the same subset as xv6's `printf`. It does not support floating-point, `%n`, `%*`, or positional arguments. This is acceptable for kernel logging.

7. **`log_dump_crash_context()` recursion safety:** If `printf()` inside `log_dump_crash_context()` triggers another panic (unlikely but possible), infinite recursion would occur. The `panicking` guard in `printf()` mitigates this by skipping lock acquisition, but a true re-entrant panic would still loop.

## 7. Automated Test Program

A user-space test program is provided at `user/logtest.c` that validates the log system programmatically.

### Tests performed

| # | Name | What it does |
|---|------|-------------|
| 1 | `kernel log_test` | Calls `logtest()` syscall which exercises `log_info`, `log_warn`, `log_debug`, `log_panic_prep`, circular buffer wrap (69 entries), format specifier coverage, and `log_flush()` |
| 2 | `dumppanic syscall` | Calls `dumppanic()` and verifies return code is 0 and size matches `sizeof(crash_context)` |
| 3 | `magic is 0 before panic` | Ensures `crash_context.magic == 0` when no panic has occurred |
| 4 | `fields zeroed before panic` | Ensures registers and process fields are zero before panic (fresh boot state) |
| 5 | `repeated log_test (5x stress)` | Runs the kernel log test 5 times to ensure no buffer corruption or lock issues |

### How to run
```bash
$ logtest
```

### Expected output
```
logtest: starting kernel panic log tests
  TEST 1: kernel log_test ... PASS
  TEST 2: dumppanic syscall ... PASS
  TEST 3: magic is 0 before panic ... PASS
  TEST 4: fields zeroed before panic ... PASS
  TEST 5: repeated log_test (5x stress) ... PASS

logtest: 5/5 tests passed
logtest: ALL TESTS PASSED
```

The kernel log flush output from `log_test()` will also appear on the console as the tests run.

## 8. Architecture Decision Record

| Decision | Rationale |
|----------|-----------|
| Static BSS vs. fixed physical address | Fixed physical addresses below KERNBASE conflict with QEMU devices; BSS is simpler and safer |
| Spinlock for log buffer | Standard xv6 concurrency primitive; adequate for infrequent logging |
| Custom vsnprintf vs. reusing printf | printf writes to UART directly; log buffer needs string storage |
| RISC-V registers (not x86) | Codebase is xv6-riscv; x86 registers do not exist on this architecture |
| Frame-pointer stack walk vs. DWARF | FP walk is simple, fast, and does not require external tools |
| Most-recent-first log flush | The most recent entries (closest to the panic) are most relevant |
| Separate syscall vs. /dev-style interface | Follows existing xv6 pattern (e.g., `uptime` syscall) |
