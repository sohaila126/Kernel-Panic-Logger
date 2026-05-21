//
// crashme — trigger a simulated kernel panic from user space.
// Runs the same capture/display pipeline as real panic(), but
// returns to the shell afterward so you can run dumppanic.
//

#include "kernel/types.h"
#include "kernel/paniclog.h"
#include "user/user.h"

int
main(void)
{
  printf("crashme: triggering kernel panic capture...\n");

  int ret = crashme();
  if (ret < 0) {
    printf("crashme: syscall failed\n");
    return -1;
  }

  printf("crashme: capture complete. run 'dumppanic' to view.\n");
  return 0;
}
