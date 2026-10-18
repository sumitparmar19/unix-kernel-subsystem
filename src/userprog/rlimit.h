#ifndef USERPROG_RLIMIT_H
#define USERPROG_RLIMIT_H

/* Per-process resource limits, "cgroups-lite" (docs/adr/ADR-004).

   LIMIT_PROCS and LIMIT_PAGES are accounted across a process and its
   descendants (a shared struct rgroup), so a fork bomb or memory hog
   cannot escape its cap by spawning more processes.  LIMIT_FDS and
   LIMIT_CPU are per-process and inherited by value. */

#include <stdbool.h>
#include <stdint.h>

enum rlimit_resource
  {
    LIMIT_PROCS,        /* Live processes in the subtree. */
    LIMIT_PAGES,        /* User pages charged to the subtree. */
    LIMIT_FDS,          /* Open descriptors in this process. */
    LIMIT_CPU,          /* CPU time for this process, in timer ticks. */
    LIMIT_CNT
  };

#define RLIMIT_NONE (-1)        /* No limit. */

/* Subtree resource group, shared by a process and its descendants.
   Groups form a tree: lowering a subtree limit (setlimit) makes a CHILD
   group under the current one, and a process is counted in that group and
   in every ancestor up to the root.  Admission and page charges walk from
   the leaf to the root and succeed only if every level is under its cap,
   so a descendant can never escape an ancestor's limit by splitting off a
   fresh group (docs/adr/ADR-004). */
struct rgroup
  {
    struct rgroup *parent;     /* Enclosing group, or NULL at the root. */
    int max_procs;             /* Cap, or RLIMIT_NONE. */
    int procs;                 /* Live members in this subtree. */
    int32_t max_pages;         /* Cap, or RLIMIT_NONE. */
    int32_t pages;             /* User pages charged in this subtree. */
    int refs;                  /* Leaf members plus child groups. */
  };

struct thread;

void rlimit_init_root (struct thread *);
void rlimit_join (struct thread *child, struct rgroup *group,
                  int max_fds, int cpu_budget);
void rlimit_exit (struct thread *);

bool rlimit_set (struct thread *, enum rlimit_resource, int value);
int rlimit_get (struct thread *, enum rlimit_resource);

/* LIMIT_PROCS: try to admit one more process to T's group. */
bool rlimit_admit_proc (struct thread *);
void rlimit_drop_proc (struct thread *);

/* LIMIT_PAGES: charge/uncharge N user pages to T's group. */
bool rlimit_charge_pages (struct thread *, int n);
void rlimit_uncharge_pages (struct thread *, int n);

#endif /* userprog/rlimit.h */
