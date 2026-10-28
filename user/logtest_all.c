#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "logtest_tests.h"

int
logtest_logging(void)
{
  printf("\n===== LOGGING CONTROL TEST =====\n");
  printf("Expectation: startLogging prints Logging Started; stopLogging prints Logging Stopped\n");
  startLogging();
  stopLogging();
  printf("Results: both logging calls returned\n");
  return 0;
}

int
logtest_all(void)
{
  printf("\n===== ALL LOGTEST CASES =====\n");
  if (logtest_logging() != 0 || logtest_boundaries() != 0 ||
      logtest_priority(0) != 0 || logtest_fairness(0) != 0 ||
      logtest_trace() != 0)
    return 1;
  printf("Results: all logtest modules passed\n");
  return 0;
}