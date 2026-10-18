# ADR-004: Per-process resource limits ("cgroups-lite")

**Status:** accepted (M5), hardened (v2.1: hierarchical groups, B1) · **Code:** `src/userprog/rlimit.[ch]`,
`syscall.c`, `process.c`, `threads/thread.[ch]`, `devices/timer.c` · **Proof:** `src/tests/limits`,
`src/tests/hardening/esc-procs`, `esc-pages`

## Context
A sandbox must stop one program from starving the others: a fork bomb, a memory hog, a descriptor
leak, a CPU spinner. The course kernel had no limits — any user program could exhaust kernel memory or
spin forever. This is the kernel-side of what Linux `cgroups` and `setrlimit` do.

## Decision

### Four resources, two scopes
| Resource | Scope | Enforced in | Over-limit action |
|---|---|---|---|
| `LIMIT_PROCS` — live processes | **subtree** (a process and its descendants) | `process_execute` | `exec` returns −1 |
| `LIMIT_PAGES` — user pages | **subtree** | user-page allocation (`frame_alloc` in M6; `load`/`setup_stack` now) | allocation fails → process killed (−1) |
| `LIMIT_FDS` — open descriptors | **per process** | `open`, `pipe`, `dup2` | the call returns −1 |
| `LIMIT_CPU` — CPU time in timer ticks | **per process** | timer interrupt | process killed (−1) |

**Why two scopes?** A *per-process* child limit cannot stop a fork bomb: each child would get its own
fresh allowance and could spawn N more, so the tree still explodes. Process and memory limits must
therefore be **shared across the whole subtree**, exactly as cgroup v2 accounts a subtree. Descriptors
and CPU are naturally per-process, like POSIX `RLIMIT_NOFILE` and `RLIMIT_CPU`.

### Resource groups — a hierarchy
Subtree limits live in reference-counted `struct rgroup`s arranged in a **tree**, so a process is
counted in its own group *and in every ancestor up to the root*:
```
struct rgroup { struct rgroup *parent;  int max_procs, procs;  int32_t max_pages, pages;  int refs; };
```
- At boot the initial process gets a root group (no parent) with unlimited caps.
- A new process **joins its parent's leaf group** at creation and leaves it at exit; the group is freed
  when its last member and child group go away, dropping a reference on its parent in turn.
- `setlimit(LIMIT_PROCS|LIMIT_PAGES, n)` makes the caller a **child group inside its current one**
  (cap `n ≤` the current cap, seeded with the caller's current usage). The caller stays counted in the
  old group and all its ancestors.
- **Admission and page charges walk from the leaf to the root, all-or-nothing:** a new process or page
  is allowed only if *every* level is under its cap, and is then counted at every level. So the subtree
  total is bounded at every ancestor no matter how descendants carve up their own sub-limits.

**The escape bug this prevents (B1, fixed in v2.1).** The original design instead let `setlimit`
**split off a flat new group** seeded with the caller's usage and *detach from the old one*. A
descendant could then call `setlimit` to its current cap, get a brand-new full-sized allowance, and
free a slot back in its parent — so every child could repeat it and the subtree grew without bound.
The existing tests missed it because their abusers capped *themselves* rather than trying to escape;
`tests/hardening/esc-procs` and `esc-pages` now attack the limit directly and confirm containment.

### Per-process limits
`max_fds` and a `cpu_budget` (ticks remaining, −1 = unlimited) live in `struct thread`. They are copied
**by value** to a child at creation, so a child can't exceed what its parent allowed, and
`setlimit` can only ever **lower** them (like `setrlimit` for an unprivileged process).

### New system calls
| Call | Semantics |
|---|---|
| `bool setlimit (int resource, int value)` | Lower a limit on the caller (and, for subtree resources, its future descendants). Raising is refused (returns false). Documented in `docs/SYSCALLS.md`. |
| `int getlimit (int resource)` | The caller's current cap for `resource` (−1 = unlimited). |
| `int uptime (void)` | Timer ticks since boot. Lets a test assert a neighbour finished *within a time bound* while an abuser ran. |

### Enforcement details
- **Fork bomb:** `process_execute` admits a child only if `procs < max_procs` at **every** level from
  the caller's group to the root, then counts it at every level; the counts fall as children exit. A
  descendant cannot escape by making its own sub-group (B1).
- **CPU spinner:** the timer interrupt decrements the running user process's `cpu_budget`; at zero it
  flags the process, which `thread_tick` turns into a kill (exit −1). Checked in the interrupt so a
  pure busy loop that makes no system calls is still caught.
- **Descriptor leak:** `open`/`pipe`/`dup2` install at the lowest free descriptor below `max_fds`.
- **Memory hog:** the user-page allocator charges the group and refuses over the cap. Pre-M6 there is
  no user-visible way to grow user memory at run time, so the mechanism is in place but the `mem-hog`
  test arrives with VM in M6.

Counters are updated with interrupts disabled (the critical sections are a few instructions and some
run from the timer interrupt), consistent with the rest of this uniprocessor kernel.

## Alternatives considered
- **A single global limit:** would make one tenant's fork bomb cap everyone, the opposite of isolation.
- **Pure per-process limits inherited by value:** simplest, but can't bound a fork bomb (above).
- **A flat "split a new group" scheme (original M5 design):** simplest, but it let a descendant mint a
  fresh full allowance and escape its ancestor's cap (B1). Replaced by the hierarchy above.
- **A full cgroup hierarchy with live re-parenting of running descendants:** more faithful to Linux;
  the extra bookkeeping to move *already-running* processes between groups buys nothing the sandbox
  needs, so limits still apply to a process's *future* descendants, but accounting now walks the whole
  ancestor chain so the subtree cap cannot be escaped.

## Consequences
- The `neighbor-survives` test proves the key property: a well-behaved process started next to each
  abuser still finishes, and within a recorded `uptime` bound.
- M6 connects `LIMIT_PAGES` to `frame_alloc`; nothing else about limits changes.
