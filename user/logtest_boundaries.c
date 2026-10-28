#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

static int
check_result(char *label, int actual, int expected)
{
  printf("Results: %s returned %d; expected %d\n", label, actual, expected);
  if (actual != expected) {
    printf("Results: %s failed\n", label);
    return 1;
  }
  return 0;
}

int
logtest_boundaries(void)
{
  int pid = getpid();
  int original_nice = nice(pid, 0);
  int value;

  printf("\n===== NICE BOUNDARIES TESTS =====\n");

  printf("Expectation: positive increment changes nice 0 to 5\n");
  startLogging();
  value = nice(pid, -original_nice);
  if (value == 0)
    value = nice(pid, 5);
  stopLogging();
  if (check_result("nice(pid, 5)", value, 5))
    return 1;

  printf("\nExpectation: negative increment changes nice 5 to 3\n");
  startLogging();
  value = nice(pid, -2);
  stopLogging();
  if (check_result("nice(pid, -2)", value, 3))
    return 1;

  printf("\nExpectation: adding -100 clamps nice at -20\n");
  startLogging();
  value = nice(pid, -100);
  stopLogging();
  if (check_result("nice(pid, -100)", value, -20))
    return 1;

  printf("\nExpectation: adding 100 clamps nice at 19\n");
  startLogging();
  value = nice(pid, 100);
  stopLogging();
  if (check_result("nice(pid, 100)", value, 19))
    return 1;

  printf("\nExpectation: valid nice 0 and invalid PID both return 0, but only the valid PID can then change\n");
  startLogging();
  int valid_zero = nice(pid, -19);
  int invalid_result = nice(999999, 3);
  int valid_after = nice(pid, 1);
  stopLogging();
  printf("Results: valid PID returned %d at nice 0; invalid PID returned %d; valid follow-up returned %d\n",
         valid_zero, invalid_result, valid_after);
  if (valid_zero != 0 || invalid_result != 0 || valid_after != 1)
    return 1;

  printf("\nExpectation: child nice starts at 0 when forked by a parent at nice 7\n");
  int nice_pipe[2];
  if (pipe(nice_pipe) < 0)
    return 1;
  startLogging();
  int parent_nice_before = nice(pid, 0);
  if (nice(pid, 7 - parent_nice_before) != 7) {
    close(nice_pipe[0]);
    close(nice_pipe[1]);
    stopLogging();
    return 1;
  }

  int child_pid = fork();
  if (child_pid < 0) {
    close(nice_pipe[0]);
    close(nice_pipe[1]);
    nice(pid, parent_nice_before - 7);
    stopLogging();
    return 1;
  }
  if (child_pid == 0) {
    close(nice_pipe[0]);
    int child_nice = nice(getpid(), 0);
    if (write(nice_pipe[1], &child_nice, sizeof(child_nice)) !=
        sizeof(child_nice))
      exit(1);
    close(nice_pipe[1]);
    exit(child_nice == 0 ? 0 : 1);
  }

  close(nice_pipe[1]);
  int child_nice = -1;
  int bytes_read = 0;
  while (bytes_read < sizeof(child_nice)) {
    int n = read(nice_pipe[0], (char *)&child_nice + bytes_read,
                 sizeof(child_nice) - bytes_read);
    if (n <= 0)
      break;
    bytes_read += n;
  }
  close(nice_pipe[0]);
  int child_status;
  int reaped_pid = wait(&child_status);
  int parent_after = nice(pid, 0);
  nice(pid, parent_nice_before - parent_after);
  stopLogging();
  printf("Results: child PID %d nice %d; parent nice %d; wait PID %d status %d\n",
         child_pid, child_nice, parent_after, reaped_pid, child_status);
  if (bytes_read != sizeof(child_nice) || child_nice != 0 || parent_after != 7 ||
      reaped_pid != child_pid || child_status != 0)
    return 1;

  return 0;
}