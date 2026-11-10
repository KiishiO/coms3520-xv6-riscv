// Project 1C: scheduling policies.
//
// This is the file you modify. The scheduler framework in proc.c
// calls the functions below; it already handles locking and the
// context switch, so a policy only decides which process runs next
// and when the running process should be preempted.
//
// Each policy has an ID. Select one at run time with the
// set_scheduler(id) system call (for example: wl 2 1). To try a
// different setting, such as another quantum, give it its own ID.
// ID 3: three-level MLFQ with nice-based starting queues and boosts.
// ID 5: MLFQ with weighted-stride selection within each level.
// ID 10: MLFQ with a 30-tick boost interval.
// ID 11: MLFQ with a 5-tick Q1 quantum.
// ID 12: MLFQ with a 5-tick Q1 allotment.
// ID 4: weighted stride scheduling with a 5-tick quantum.
// ID 6: round robin with a 5-tick (50 ms) quantum.
//
// Rules for policy code (Section 2.2 of the project document):
//  1. The system has one CPU. These functions run with interrupts off
//     and must not sleep or acquire p->lock (the framework holds the
//     locks it needs).
//  2. policy_pick_next_proc() must only return a RUNNABLE process, or
//     0 if there is none.
//  3. A scheduler meant to be fair must not starve a job: every ready
//     job eventually runs. (Strict priority, ID 2, is the one
//     deliberate exception.)
//  4. A scheduler must not recognize a workload: do not read
//     p->wl_slot, workload IDs, process names, or pids to decide how
//     to treat a process.
//  5. policy_init(id) may be called more than once for the same ID,
//     including at the start of every workload run. Each call must
//     reset all of the scheduler's global state.
//  6. Do not modify the protected files (kernel/workload.c,
//     kernel/workload.h, user/wl.c).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define SCHED_RR     0 // round robin, 10-tick (100 ms) quantum: stock xv6
#define SCHED_RR1    1 // round robin, 1-tick (10 ms) quantum
#define SCHED_PRIO   2 // strict priority by nice (Project 1B), 10-tick quantum
#define SCHED_MLFQ   3 // three-level MLFQ
#define SCHED_STRIDE 4 // weighted stride scheduling, 5-tick quantum
#define SCHED_MLFQ_STRIDE 5 // MLFQ with weighted-stride selection per level
#define SCHED_RR5    6 // round robin, 5-tick (50 ms) quantum
#define SCHED_MLFQ_BOOST30 10
#define SCHED_MLFQ_Q1Q5    11
#define SCHED_MLFQ_Q1A5    12

#define Q0 0
#define Q1 1
#define Q2 2

static int mlfq_quantum[3] = { 15, 10, 2 };
static int mlfq_allotment[3] = { 0, 10, 2 };
static int mlfq_boost_interval = 60;
static int last_at[3] = { -1, -1, -1 };
static uint last_boost;

// Linux CFS weights for nice -20 .. 19, indexed by nice + 20
// (nice 0 has weight 1024). This is the same table user/wl.c uses
// for the fairness metric. For example, a fair-share scheduler can
// advance a process's vrt by (1 << 24) / nice_weight[p->nice + 20]
// for each tick it runs.
const int nice_weight[40] = {
  88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
  9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
  1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
  110,   87,    70,    56,    45,    36,    29,    23,    18,    15,
};

static int quantum = 10; // ticks a process may run before preemption
static int last = -1;    // index in proc[] of the last process chosen
static int last_stride = -1; // index used to rotate stride ties

static int
is_mlfq_policy(int id)
{
    return id == SCHED_MLFQ || id == SCHED_MLFQ_STRIDE ||
         id == SCHED_MLFQ_BOOST30 || id == SCHED_MLFQ_Q1Q5 ||
         id == SCHED_MLFQ_Q1A5;
}

static uint64
stride_for_nice(int nice)
{
  return ((uint64)1 << 24) / nice_weight[nice + 20];
}

static int
queue_for_nice(int nice)
{
  if (nice <= -10)
    return Q2;
  if (nice <= 10)
    return Q1;
  return Q0;
}

static void
set_level(struct proc *p, int level)
{
  int old = p->level;

  if (old == level)
    return;
  p->level = level;
  if (logging)
    printk("level %d %d->%d at %d\n", p->pid, old, level, ticks);
}

static void
mlfq_boost_if_due(void)
{
  if ((uint)(ticks - last_boost) < (uint)mlfq_boost_interval)
    return;

  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    set_level(p, p->start > Q1 ? p->start : Q1);
    p->used = 0;
  }
  last_boost = ticks;
}

static struct proc *
pick_mlfq(void)
{
  mlfq_boost_if_due();
  for (int level = Q2; level >= Q0; level--) {
    for (int i = 1; i <= NPROC; i++) {
      struct proc *p = &proc[(last_at[level] + i) % NPROC];
      if (p->state == RUNNABLE && p->level == level) {
        last_at[level] = p - proc;
        return p;
      }
    }
  }
  return 0;
}

static struct proc *
pick_mlfq_stride(void)
{
  mlfq_boost_if_due();
  for (int level = Q2; level >= Q0; level--) {
    uint64 min_pass = ~(uint64)0;
    for (struct proc *p = proc; p < &proc[NPROC]; p++)
      if (p->state == RUNNABLE && p->level == level && p->vrt < min_pass)
        min_pass = p->vrt;

    if (min_pass == ~(uint64)0)
      continue;

    for (int i = 1; i <= NPROC; i++) {
      struct proc *p = &proc[(last_at[level] + i) % NPROC];
      if (p->state == RUNNABLE && p->level == level && p->vrt == min_pass) {
        last_at[level] = p - proc;
        return p;
      }
    }
  }
  return 0;
}

// Called by set_scheduler(id), and again when a workload starts.
// Set up global state for policy id.
// Return 0 if id is a known policy, -1 otherwise.
int
policy_init(int id)
{
  switch (id) {
  case SCHED_RR:
  case SCHED_RR1:
  case SCHED_PRIO:
  case SCHED_MLFQ:
  case SCHED_MLFQ_STRIDE:
  case SCHED_STRIDE:
  case SCHED_RR5:
  case SCHED_MLFQ_BOOST30:
  case SCHED_MLFQ_Q1Q5:
  case SCHED_MLFQ_Q1A5:
    break;
  default:
    return -1;
  }

  mlfq_quantum[Q0] = 15;
  mlfq_quantum[Q1] = 10;
  mlfq_quantum[Q2] = 2;
  mlfq_allotment[Q0] = 0;
  mlfq_allotment[Q1] = 10;
  mlfq_allotment[Q2] = 2;
  mlfq_boost_interval = 60;

  switch (id) {
  case SCHED_RR:
  case SCHED_PRIO:
    quantum = 10;
    break;
  case SCHED_RR1:
    quantum = 1;
    break;
  case SCHED_RR5:
    quantum = 5;
    break;
  case SCHED_MLFQ:
  case SCHED_MLFQ_STRIDE:
  case SCHED_MLFQ_BOOST30:
  case SCHED_MLFQ_Q1Q5:
  case SCHED_MLFQ_Q1A5:
    quantum = 10;
    break;
  case SCHED_STRIDE:
    quantum = 5;
    break;
  default:
    break;
  }
  if (id == SCHED_MLFQ_BOOST30)
    mlfq_boost_interval = 30;
  else if (id == SCHED_MLFQ_Q1Q5)
    mlfq_quantum[Q1] = 5;
  else if (id == SCHED_MLFQ_Q1A5)
    mlfq_allotment[Q1] = 5;
  else if (id == SCHED_MLFQ_STRIDE)
    mlfq_allotment[Q1] = 5;

  last = -1;
  last_stride = -1;
  for (int i = 0; i < 3; i++)
    last_at[i] = -1;
  last_boost = ticks;
  return 0;
}

// Called when a process is created, and for every process when the
// policy changes or a workload starts. Initialize any per-process
// policy fields.
void
policy_proc_init(struct proc *p)
{
  p->qticks = 0;
  // Level 0 is the lowest queue (Q0); Q2 is the highest. An MLFQ must
  // set the starting level from p->nice here: workload_start() calls
  // this for every process after the jobs' nice values are set.
  p->start = queue_for_nice(p->nice);
  p->level = is_mlfq_policy(sched_current) ? p->start : Q0;
  p->used = 0;
  p->vrt = 0;
  p->stride = stride_for_nice(p->nice);
}

// Round robin: the next RUNNABLE process after the last one chosen,
// scanning proc[] in a circle so no process is starved.
static struct proc *
pick_rr(void)
{
  for (int i = 1; i <= NPROC; i++) {
    struct proc *p = &proc[(last + i) % NPROC];
    if (p->state == RUNNABLE) {
      last = p - proc;
      return p;
    }
  }
  return 0;
}

// Strict priority: a RUNNABLE process with the lowest nice value;
// processes with equal nice take turns in round-robin order.
static struct proc *
pick_prio(void)
{
  int best = 100;

  for (struct proc *p = proc; p < &proc[NPROC]; p++)
    if (p->state == RUNNABLE && p->nice < best)
      best = p->nice;
  for (int i = 1; i <= NPROC; i++) {
    struct proc *p = &proc[(last + i) % NPROC];
    if (p->state == RUNNABLE && p->nice == best) {
      last = p - proc;
      return p;
    }
  }
  return 0;
}

static struct proc *
pick_stride(void)
{
  uint64 min_pass = ~(uint64)0;

  for (struct proc *p = proc; p < &proc[NPROC]; p++)
    if (p->state == RUNNABLE && p->vrt < min_pass)
      min_pass = p->vrt;

  if (min_pass == ~(uint64)0)
    return 0;

  for (int i = 1; i <= NPROC; i++) {
    struct proc *p = &proc[(last_stride + i) % NPROC];
    if (p->state == RUNNABLE && p->vrt == min_pass) {
      last_stride = p - proc;
      return p;
    }
  }
  return 0;
}

// Choose the next process to run, or return 0 if none is RUNNABLE.
struct proc *
policy_pick_next_proc(void)
{
  switch (sched_current) {
  case SCHED_PRIO:
    return pick_prio();
  case SCHED_MLFQ:
  case SCHED_MLFQ_BOOST30:
  case SCHED_MLFQ_Q1Q5:
  case SCHED_MLFQ_Q1A5:
    return pick_mlfq();
  case SCHED_MLFQ_STRIDE:
    return pick_mlfq_stride();
  case SCHED_STRIDE:
    return pick_stride();
  case SCHED_RR5:
    return pick_rr();
  default:
    return pick_rr();
  }
}

// Called just before the framework switches to p.
void
policy_dispatch(struct proc *p)
{
  p->qticks = 0; // a new quantum starts
}

// Called on every timer tick while p is running.
// Return 1 to preempt p, or 0 to let it keep running.
// A process is charged only for ticks that arrive while it is running.
// A loop of pause(1) wakes at a tick, runs briefly, and sleeps again
// before the next one, so it is never charged and cannot test a rule
// such as an allotment kept across sleeps. A test that is charged must
// keep the CPU across a tick, for example by spinning until uptime()
// changes before each pause(1); workload jobs are charged correctly.
int
policy_tick(struct proc *p)
{
  if (sched_current == SCHED_STRIDE) {
    p->vrt += p->stride;
    p->qticks++;
    return p->qticks >= quantum;
  }
  if (is_mlfq_policy(sched_current)) {
    if (sched_current == SCHED_MLFQ_STRIDE)
      p->vrt += p->stride;
    p->used++;
    p->qticks++;
    mlfq_boost_if_due();

    int quantum_expired = p->qticks >= mlfq_quantum[p->level];
    if (mlfq_allotment[p->level] > 0 &&
        p->used >= mlfq_allotment[p->level]) {
      set_level(p, p->level - 1);
      p->used = 0;
    }

    int preempt = quantum_expired;
    for (struct proc *q = proc; q < &proc[NPROC]; q++)
      if (q->state == RUNNABLE && q->level > p->level)
        preempt = 1;
    return preempt;
  }
  p->qticks++;
  return p->qticks >= quantum;
}

// Called when p wakes from sleep (for example, when its simulated I/O
// or a pause() ends) and becomes RUNNABLE again. Use it to decide how
// a waking process re-enters the policy; for example, a fair-share or
// stride policy may move a process that slept a long time up to the
// current minimum, so it cannot claim the CPU for the time it slept.
void
policy_wakeup(struct proc *p)
{
  if (sched_current == SCHED_STRIDE || sched_current == SCHED_MLFQ_STRIDE) {
    uint64 min_pass = ~(uint64)0;
    for (struct proc *q = proc; q < &proc[NPROC]; q++) {
      if (q == p || (q->state != RUNNING && q->state != RUNNABLE))
        continue;
      if (q->vrt < min_pass)
        min_pass = q->vrt;
    }
    if (min_pass != ~(uint64)0 && p->vrt < min_pass)
      p->vrt = min_pass;
  }
}

// Called whenever p's nice value changes.
void
policy_nice(struct proc *p)
{
  if (sched_current == SCHED_STRIDE || sched_current == SCHED_MLFQ_STRIDE)
    p->stride = stride_for_nice(p->nice);
  if (is_mlfq_policy(sched_current)) {
    int start = queue_for_nice(p->nice);
    if (start != p->start) {
      p->start = start;
      set_level(p, start);
      p->used = 0;
    }
  }
}
