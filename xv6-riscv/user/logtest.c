//
// logtest — automated test for the kernel panic log system.
//

#include "kernel/types.h"
#include "kernel/paniclog.h"
#include "user/user.h"

// ============================================================================
// SECTION: Test macros
// ============================================================================
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
  printf("  TEST %d: %s ... ", ++tests_run, name); \
} while(0)

#define PASS() do { \
  printf("PASS\n"); \
  tests_passed++; \
} while(0)

#define FAIL(msg) do { \
  printf("FAIL: %s\n", msg); \
} while(0)

// ============================================================================
// SECTION: main — test runner
// ============================================================================
int
main(void)
{
  printf("logtest: starting kernel panic log tests\n");

  TEST("crash context magic is 0 before panic");
  {
    struct crash_context ctx;
    uint64 sz;
    int r = dumppanic(&ctx, &sz);
    if (r == 0 && sz == sizeof(ctx) && ctx.magic == 0)
      PASS();
    else
      FAIL("expected magic=0");
  }

  TEST("crash context fields are zeroed before panic");
  {
    struct crash_context ctx;
    uint64 sz;
    dumppanic(&ctx, &sz);
    if (ctx.ra == 0 && ctx.sp == 0 && ctx.sepc == 0 && ctx.pid == 0)
      PASS();
    else
      FAIL("some fields non-zero before panic");
  }

  TEST("kernel log_test (info/warn/debug/flush + crash ctx)");
  int ret = logtest();
  if (ret == 0)
    PASS();
  else
    FAIL("logtest() returned non-zero");

  TEST("dumppanic syscall returns valid crash context");
  {
    struct crash_context ctx;
    uint64 sz;
    ret = dumppanic(&ctx, &sz);
    if (ret == 0 && sz == sizeof(ctx))
      PASS();
    else
      FAIL("syscall failed or size mismatch");
  }

  TEST("crash context populated after log_test");
  {
    struct crash_context ctx;
    uint64 sz;
    dumppanic(&ctx, &sz);
    if (ctx.magic == 0xDEADBEEF && ctx.ra != 0)
      PASS();
    else
      FAIL("context not populated");
  }

  TEST("repeated log_test calls (stress)");
  {
    int ok = 1;
    for (int i = 0; i < 5; i++) {
      if (logtest() != 0) {
        ok = 0;
        break;
      }
    }
    if (ok) PASS();
    else FAIL("logtest failed on iteration");
  }

  printf("\nlogtest: %d/%d tests passed\n", tests_passed, tests_run);
  if (tests_passed == tests_run) {
    printf("logtest: ALL TESTS PASSED\n");
    return 0;
  } else {
    printf("logtest: SOME TESTS FAILED\n");
    return 1;
  }
}
