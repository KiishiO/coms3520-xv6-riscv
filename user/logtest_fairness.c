#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

int
logtest_fairness(int require_alternation)
{
  int events[8] = {0};
  int first_pid;
  int second_pid;
  int setup;
  int first_seen = 0;
  int second_seen = 0;

  printf("\n===== EQUAL-NICE FAIRNESS TEST =====\n");
  printf("Expectation: equal-nice PIDs both take turns%s\n",
         require_alternation ? " in alternating order" : "");
  startLogging();
  setup = logtest_run_sched_pair(0, 0, 4, 1, events, &first_pid,
                                 &second_pid);
  stopLogging();
  if (setup != 0) {
    printf("Results: fairness setup failed (%d)\n", setup);
    return 1;
  }

  for (int i = 0; i < 8; i++) {
    if (events[i] == first_pid)
      first_seen = 1;
    else if (events[i] == second_pid)
      second_seen = 1;
    else
      return 1;
    if (require_alternation && i > 0 && events[i] == events[i - 1]) {
      printf("Results: PID %d received consecutive work events\n", events[i]);
      return 1;
    }
  }
  if (!first_seen || !second_seen)
    return 1;

  printf("Results: PIDs %d and %d both ran; event PID sequence = ",
         first_pid, second_pid);
  for (int i = 0; i < 8; i++)
    printf("%d%s", events[i], i == 7 ? "\n" : " then ");
  return 0;
}