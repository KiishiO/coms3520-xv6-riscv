#include "kernel/types.h"
#include "user/user.h"

#define WORK_TICKS 24

static void
run_ticks(int count)
{
  for (int i = 0; i < count; i++) {
    int before = uptime();
    while (uptime() == before) {
    }
    pause(1);
  }
}

static void
child_work(int index)
{
  int value;

  if (index == 0) {
    printf("mlfqtest: child 0 CPU-bound, nice 0, pid %d\n", getpid());
    run_ticks(WORK_TICKS);
  } else if (index == 1) {
    printf("mlfqtest: child 1 nice-change test, pid %d\n", getpid());
    value = nice(getpid(), -10);
    printf("mlfqtest: child 1 nice 0 -> %d (Q2)\n", value);
    value = nice(getpid(), -1);
    printf("mlfqtest: child 1 same-queue nice -> %d\n", value);
    run_ticks(WORK_TICKS);
  } else {
    printf("mlfqtest: child 2 Q0 worker, pid %d\n", getpid());
    value = nice(getpid(), 11);
    printf("mlfqtest: child 2 nice 0 -> %d (Q0)\n", value);
    run_ticks(WORK_TICKS);
  }
  printf("mlfqtest: child %d finished\n", index);
}

int
main(void)
{
  int created = 0;

  if (set_scheduler(3) < 0) {
    printf("mlfqtest: set_scheduler(3) failed\n");
    exit(1);
  }
  startLogging();

  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("mlfqtest: fork failed\n");
      break;
    }
    if (pid == 0) {
      child_work(i);
      exit(0);
    }
    created++;
  }

  printf("mlfqtest: parent waiting for %d children\n", created);
  for (int i = 0; i < created; i++)
    wait(0);

  stopLogging();
  printf("mlfqtest: all children reaped\n");
  exit(created == 3 ? 0 : 1);
}
