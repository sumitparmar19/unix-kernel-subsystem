#include "userprog/rlimit.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* All counters are touched with interrupts off: the critical sections
   are a few instructions, and some run from the timer interrupt. */

static struct rgroup *
rgroup_create (int max_procs, int32_t max_pages)
{
  struct rgroup *g = malloc (sizeof *g);
  if (g != NULL)
    {
      g->max_procs = max_procs;
      g->procs = 1;             /* The creating process is the first member. */
      g->max_pages = max_pages;
      g->pages = 0;
      g->refs = 1;
    }
  return g;
}

static void
rgroup_unref (struct rgroup *g)
{
  bool last;
  enum intr_level old = intr_disable ();
  last = --g->refs == 0;
  intr_set_level (old);
  if (last)
    free (g);
}

/* Gives the initial process an unlimited root group and no per-process
   caps. */
void
rlimit_init_root (struct thread *t)
{
  t->rgroup = rgroup_create (RLIMIT_NONE, RLIMIT_NONE);
  t->max_fds = RLIMIT_NONE;
  t->cpu_budget = RLIMIT_NONE;
  t->charged_pages = 0;
  t->over_cpu = false;
}

/* A new CHILD joins GROUP (whose process slot the parent already
   reserved with rlimit_admit_proc) and takes the given per-process
   caps.  Call with the child not yet running. */
void
rlimit_join (struct thread *child, struct rgroup *group,
             int max_fds, int cpu_budget)
{
  enum intr_level old = intr_disable ();
  child->rgroup = group;
  group->refs++;               /* procs was bumped by rlimit_admit_proc(). */
  intr_set_level (old);
  child->max_fds = max_fds;
  child->cpu_budget = cpu_budget;
  child->charged_pages = 0;
  child->over_cpu = false;
}

/* Releases T's group membership and returns the pages it charged. */
void
rlimit_exit (struct thread *t)
{
  enum intr_level old = intr_disable ();
  if (t->rgroup != NULL)
    {
      t->rgroup->procs--;
      t->rgroup->pages -= t->charged_pages;
      t->charged_pages = 0;
    }
  intr_set_level (old);
  if (t->rgroup != NULL)
    {
      rgroup_unref (t->rgroup);
      t->rgroup = NULL;
    }
}

/* Lowers a limit on T.  Raising, or an unknown resource, fails.  For
   subtree resources T splits off a new, tighter group that it and its
   future descendants share. */
bool
rlimit_set (struct thread *t, enum rlimit_resource res, int value)
{
  if (value < 0)
    return false;
  switch (res)
    {
    case LIMIT_FDS:
      if (t->max_fds != RLIMIT_NONE && value > t->max_fds)
        return false;
      t->max_fds = value;
      return true;
    case LIMIT_CPU:
      if (t->cpu_budget != RLIMIT_NONE && value > t->cpu_budget)
        return false;
      t->cpu_budget = value;
      return true;
    case LIMIT_PROCS:
    case LIMIT_PAGES:
      {
        struct rgroup *old_g = t->rgroup, *g;
        int mp = old_g->max_procs, pg = old_g->max_pages;
        if (res == LIMIT_PROCS)
          {
            if (mp != RLIMIT_NONE && value > mp)
              return false;
            mp = value;
          }
        else
          {
            if (pg != RLIMIT_NONE && value > pg)
              return false;
            pg = value;
          }
        g = rgroup_create (mp, pg);
        if (g == NULL)
          return false;
        /* Carry this process's existing page charge into the new group. */
        g->pages = t->charged_pages;
        {
          enum intr_level old = intr_disable ();
          t->rgroup = g;
          old_g->procs--;
          old_g->pages -= t->charged_pages;
          intr_set_level (old);
        }
        rgroup_unref (old_g);
        return true;
      }
    default:
      return false;
    }
}

int
rlimit_get (struct thread *t, enum rlimit_resource res)
{
  switch (res)
    {
    case LIMIT_PROCS: return t->rgroup->max_procs;
    case LIMIT_PAGES: return t->rgroup->max_pages;
    case LIMIT_FDS:   return t->max_fds;
    case LIMIT_CPU:   return t->cpu_budget;
    default:          return RLIMIT_NONE;
    }
}

/* Admits one more process to T's group if under the cap. */
bool
rlimit_admit_proc (struct thread *t)
{
  struct rgroup *g = t->rgroup;
  bool ok;
  enum intr_level old = intr_disable ();
  ok = g->max_procs == RLIMIT_NONE || g->procs < g->max_procs;
  if (ok)
    g->procs++;
  intr_set_level (old);
  return ok;
}

/* Undoes a rlimit_admit_proc() for a child that was never created. */
void
rlimit_drop_proc (struct thread *t)
{
  enum intr_level old = intr_disable ();
  t->rgroup->procs--;
  intr_set_level (old);
}

/* Charges N user pages to T's group, if under the cap. */
bool
rlimit_charge_pages (struct thread *t, int n)
{
  struct rgroup *g = t->rgroup;
  bool ok;
  enum intr_level old = intr_disable ();
  ok = g->max_pages == RLIMIT_NONE || g->pages + n <= g->max_pages;
  if (ok)
    {
      g->pages += n;
      t->charged_pages += n;
    }
  intr_set_level (old);
  return ok;
}

void
rlimit_uncharge_pages (struct thread *t, int n)
{
  enum intr_level old = intr_disable ();
  t->rgroup->pages -= n;
  t->charged_pages -= n;
  intr_set_level (old);
}
