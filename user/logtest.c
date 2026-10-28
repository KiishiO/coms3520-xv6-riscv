#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

int
logtest_trace(void)
{
  int child_pid;
  int reaped_pid;
  int child_status;

  printf("\n===== CHILD TRACE TEST =====\n");
  printf("Expectation: child PID is traced while parent waits; stop logging after child exits\n");
  startLogging();

  child_pid = fork();
  if (child_pid < 0) {
    stopLogging();
    printf("Results: fork failed\n");
    return 1;
  }
  if (child_pid == 0) {
    int child_nice = nice(getpid(), 10);
    if (child_nice != 10)
      exit(1);

    uint start = uptime();
    volatile uint work = 0;
    while ((uint)(uptime() - start) < 3) {
      for (uint i = 0; i < 1000; i++)
        work += i;
    }
    exit(0);
  }

  reaped_pid = wait(&child_status);
  stopLogging();
  printf("Results: child PID %d exited with status %d; wait returned %d\n",
         child_pid, child_status, reaped_pid);
  return reaped_pid == child_pid && child_status == 0 ? 0 : 1;
}

int
main(int argc, char *argv[])
{
  int result;

  if (argc == 1 || (argc == 2 && strcmp(argv[1], "trace") == 0))
    result = logtest_trace();
  else if (argc == 2 && strcmp(argv[1], "all") == 0)
    result = logtest_all();
  else if (argc == 2 && strcmp(argv[1], "priority") == 0)
    result = logtest_priority(1);
  else if (argc == 2 && strcmp(argv[1], "boundaries") == 0)
    result = logtest_boundaries();
  else if (argc == 2 && strcmp(argv[1], "fairness") == 0)
    result = logtest_fairness(1);
  else {
    printf("Usage: logtest [all|trace|priority|boundaries|fairness]\n");
    exit(1);
  }

  exit(result);
}
