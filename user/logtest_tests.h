#ifndef LOGTEST_TESTS_H
#define LOGTEST_TESTS_H

int logtest_trace(void);
int logtest_logging(void);
int logtest_boundaries(void);
int logtest_priority(int require_priority_order);
int logtest_fairness(int require_alternation);
int logtest_all(void);
int logtest_run_sched_pair(int first_nice, int second_nice, int rounds,
						   int wait_for_tick, int *events,
						   int *first_pid_out, int *second_pid_out);

#endif
