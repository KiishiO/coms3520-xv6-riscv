#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

static void
priority_worker(int gate_read, int gate_write, int done_read, int done_write)
{
  char token;

  close(gate_write);
  close(done_read);
  if (read(gate_read, &token, 1) != 1)
    exit(1);

  int pid = getpid();
  int assigned_nice = nice(pid, 0);
  printf("Progress: child PID %d has nice +%d and starts work\n", pid,
         assigned_nice);
  uint start = uptime();
  volatile uint work = 0;
  while ((uint)(uptime() - start) < 5) {
    for (uint i = 0; i < 1000; i++)
      work += i;
  }

  if (write(done_write, &assigned_nice, 1) != 1)
    exit(1);
  printf("Progress: child PID %d with nice +%d finished work\n", pid,
         assigned_nice);
  close(gate_read);
  close(done_write);
  exit(0);
}

int
logtest_priority(int require_priority_order)
{
  int gate[2];
  int done[2];
  int parent_pid = getpid();
  int original_nice = nice(parent_pid, 0);
  int high_pid;
  int low_pid;
  char releases[2] = {'G', 'G'};
  char completion[2];
  int bytes_read = 0;

  printf("\n===== PRIORITY TEST =====\n");
  printf("Expectation: RUNNABLE nice +5 is selected before RUNNABLE nice +10\n");
  startLogging();
  if (nice(parent_pid, -100) != -20 || pipe(gate) < 0 || pipe(done) < 0) {
    stopLogging();
    printf("Results: priority-test setup failed\n");
    return 1;
  }

  // Create +10 first; its worker blocks at the gate until +5 is also set.
  printf("Progress: creating the +10 child first\n");
  high_pid = fork();
  if (high_pid < 0) {
    stopLogging();
    return 1;
  }
  if (high_pid == 0)
    priority_worker(gate[0], gate[1], done[0], done[1]);
  if (nice(high_pid, 10) != 10) {
    stopLogging();
    return 1;
  }
  printf("Progress: created +10 child PID %d\n", high_pid);

  // Create +5 second so the lower-nice process occupies the later table slot.
  printf("Progress: creating the +5 child second\n");
  low_pid = fork();
  if (low_pid < 0) {
    stopLogging();
    return 1;
  }
  if (low_pid == 0)
    priority_worker(gate[0], gate[1], done[0], done[1]);
  if (nice(low_pid, 5) != 5) {
    stopLogging();
    return 1;
  }
  printf("Progress: created +5 child PID %d\n", low_pid);

  close(gate[0]);
  close(done[1]);
  if (write(gate[1], releases, sizeof(releases)) != sizeof(releases)) {
    stopLogging();
    return 1;
  }
  close(gate[1]);

  int first_status;
  int second_status;
  if (wait(&first_status) < 0 || wait(&second_status) < 0 ||
      first_status != 0 || second_status != 0) {
    stopLogging();
    return 1;
  }
  while (bytes_read < sizeof(completion)) {
    int n = read(done[0], completion + bytes_read,
                 sizeof(completion) - bytes_read);
    if (n <= 0)
      break;
    bytes_read += n;
  }
  close(done[0]);
  int parent_after = nice(parent_pid, 0);
  nice(parent_pid, original_nice - parent_after);
  stopLogging();

  printf("Results: created +10 PID %d first, +5 PID %d second; completion nice values +%d then +%d\n",
         high_pid, low_pid, completion[0], completion[1]);
  if (bytes_read != sizeof(completion))
    return 1;
  if (completion[0] != 5) {
    printf("Results: +5 did not finish before +10; on SMP an already-running process is not preempted\n");
    return require_priority_order ? 1 : 0;
  }
  printf("Results: completion order matches the priority prediction\n");
  return 0;
}