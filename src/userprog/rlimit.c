#include "userprog/rlimit.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* All counters are touched with interrupts off: the critical sections
   are a few instructions, and some run from the timer interrupt. */

/* Creates an empty group under PARENT (NULL for the root), holding a
   reference on PARENT.  Members and charges are added by the caller. */
static struct rgroup *
rgroup_create (struct rgroup *parent, int max_procs, int32_t max_pages)
{
  struct rgroup *g = malloc (sizeof *g);
  if (g != NULL)
    {
      g->parent = parent;
      g->max_procs = max_procs;
      g->procs = 0;
      g->max_pages = max_pages;
      g->pages = 0;
      g->refs = 1;
      if (parent != NULL)
        {
          enum intr_level old = intr_disable ();
          parent->refs++;
          intr_set_level (old);
        }
    }
  return g;
}

/* Drops one reference on G, freeing it when the last goes away and, in
   turn, dropping G's reference on its parent. */
static void
rgroup_unref (struct rgroup *g)
{
  while (g != NULL)
    {
      struct rgroup *parent;
      bool last;
      enum intr_level old = intr_disable ();
      last = --g->refs == 0;
      intr_set_level (old);
      if (!last)
        break;
      parent = g->parent;
      free (g);
      g = parent;               /* Release the ref this group held on it. */
    }
}

/* Gives the initial process an unlimited root group and no per-process
   caps. */
void
rlimit_init_root (struct thread *t)
{
  t->rgroup = rgroup_create (NULL, RLIMIT_NONE, RLIMIT_NONE);
  t->rgroup->procs = 1;         /* The initial process is the first member. */
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

/* Releases T's group membership and the pages it charged, at every level
   from its leaf group up to the root. */
void
rlimit_exit (struct thread *t)
{
  struct rgroup *leaf = t->rgroup;
  if (leaf != NULL)
    {
      struct rgroup *g;
      enum intr_level old = intr_disable ();
      for (g = leaf; g != NULL; g = g->parent)
        {
          g->procs--;
          g->pages -= t->charged_pages;
        }
      t->charged_pages = 0;
      intr_set_level (old);
      rgroup_unref (leaf);
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
        /* Make a CHILD group under the current one and move into it.  The
           caller stays counted in old_g and every ancestor, so its tighter
           cap can only ever be below theirs -- it cannot hand itself a fresh
           full allowance.  old_g is NOT decremented; the child holds it as
           its parent. */
        g = rgroup_create (old_g, mp, pg);
        if (g == NULL)
          return false;
        g->procs = 1;                   /* This process. */
        g->pages = t->charged_pages;    /* Its pages, now also counted here. */
        {
          enum intr_level old = intr_disable ();
          t->rgroup = g;
          intr_set_level (old);
        }
        rgroup_unref (old_g);           /* Drop T's old leaf ref; parent keeps it. */
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

/* Admits one more process to T's subtree if every level from T's group
   up to the root is under its cap.  All-or-nothing. */
bool
rlimit_admit_proc (struct thread *t)
{
  struct rgroup *g;
  bool ok = true;
  enum intr_level old = intr_disable ();
  for (g = t->rgroup; g != NULL; g = g->parent)
    if (g->max_procs != RLIMIT_NONE && g->procs >= g->max_procs)
      { ok = false; break; }
  if (ok)
    for (g = t->rgroup; g != NULL; g = g->parent)
      g->procs++;
  intr_set_level (old);
  return ok;
}

/* Undoes a rlimit_admit_proc() for a child that was never created. */
void
rlimit_drop_proc (struct thread *t)
{
  struct rgroup *g;
  enum intr_level old = intr_disable ();
  for (g = t->rgroup; g != NULL; g = g->parent)
    g->procs--;
  intr_set_level (old);
}

/* Charges N user pages to T's subtree if every level is under its cap. */
bool
rlimit_charge_pages (struct thread *t, int n)
{
  struct rgroup *g;
  bool ok = true;
  enum intr_level old = intr_disable ();
  for (g = t->rgroup; g != NULL; g = g->parent)
    if (g->max_pages != RLIMIT_NONE && g->pages + n > g->max_pages)
      { ok = false; break; }
  if (ok)
    {
      for (g = t->rgroup; g != NULL; g = g->parent)
        g->pages += n;
      t->charged_pages += n;
    }
  intr_set_level (old);
  return ok;
}

void
rlimit_uncharge_pages (struct thread *t, int n)
{
  struct rgroup *g;
  enum intr_level old = intr_disable ();
  for (g = t->rgroup; g != NULL; g = g->parent)
    g->pages -= n;
  t->charged_pages -= n;
  intr_set_level (old);
}
