#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

static void
sched_worker(int gatefd, int readyfd, int eventfd, int nice_value, int rounds,
             int wait_for_tick)
{
  char token;
  char ready_token = 'R';
  int pid = getpid();

  if (nice(pid, nice_value) != nice_value)
    exit(1);
  if (write(readyfd, &ready_token, 1) != 1)
    exit(1);
  if (read(gatefd, &token, 1) != 1)
    exit(1);

  for (int round = 0; round < rounds; round++) {
    if (write(eventfd, &pid, sizeof(pid)) != sizeof(pid))
      exit(1);
    if (wait_for_tick) {
      uint start = uptime();
      while (uptime() == start)
        ;
    }
  }
  exit(0);
}

// Both workers reach the gate before release; events contain actual PIDs.
int
logtest_run_sched_pair(int first_nice, int second_nice, int rounds,
                       int wait_for_tick, int *events, int *first_pid_out,
                       int *second_pid_out)
{
  int gate[2];
  int ready[2];
  int event[2];
  int first_pid;
  int second_pid;
  int count = 0;
  int event_bytes = rounds * 2 * sizeof(int);
  int ready_count = 0;
  int original_nice = nice(getpid(), 0);
  char ready_events[2];
  char tokens[2] = {'G', 'G'};

  if (pipe(gate) < 0 || pipe(ready) < 0 || pipe(event) < 0)
    return 1;
  if (nice(getpid(), -100) != -20)
    return 2;

  first_pid = fork();
  if (first_pid == 0) {
    close(gate[1]);
    close(ready[0]);
    close(event[0]);
    sched_worker(gate[0], ready[1], event[1], first_nice, rounds,
                 wait_for_tick);
  }
  if (first_pid < 0)
    return 3;

  second_pid = fork();
  if (second_pid == 0) {
    close(gate[1]);
    close(ready[0]);
    close(event[0]);
    sched_worker(gate[0], ready[1], event[1], second_nice, rounds,
                 wait_for_tick);
  }
  if (second_pid < 0)
    return 4;
  *first_pid_out = first_pid;
  *second_pid_out = second_pid;

  close(gate[0]);
  close(ready[1]);
  close(event[1]);
  while (ready_count < sizeof(ready_events)) {
    int n = read(ready[0], ready_events + ready_count,
                 sizeof(ready_events) - ready_count);
    if (n <= 0)
      return 5;
    ready_count += n;
  }
  if (write(gate[1], tokens, sizeof(tokens)) != sizeof(tokens))
    return 6;
  close(gate[1]);

  if (nice(getpid(), 100) != 19)
    return 7;
  while (count < event_bytes) {
    int n = read(event[0], (char *)events + count, event_bytes - count);
    if (n <= 0)
      return 8;
    count += n;
  }
  close(ready[0]);
  close(event[0]);

  int first_status;
  int second_status;
  if (wait(&first_status) < 0 || wait(&second_status) < 0 ||
      first_status != 0 || second_status != 0)
    return 9;
  if (nice(getpid(), original_nice - 19) != original_nice)
    return 10;
  return 0;
}