# ADR-001: Validating every user pointer against the page tables

**Status:** accepted (M2) · **Code:** `src/userprog/umem.[ch]`, `src/userprog/syscall.c`,
`src/userprog/exception.c` · **Claim:** C7, "page-table validation of every user pointer"

## Context
A system call receives pointers chosen by an untrusted program: the stack pointer, each 4-byte
argument slot, buffers, path names and command lines. If the kernel dereferences a bad one, either the
kernel faults (in the course version, `kill` then **panics the whole kernel**) or it silently reads
kernel memory.

The course version checked only the first byte of the stack pointer, of strings, and the two ends of
buffers. It never checked the argument slots (`*(esp + 1)`). Phase 1 measured the consequences:
`sc-bad-arg` exited with a value read from kernel memory, and `sc-boundary-3`, `exec-bound-2` and
`exec-bound-3` panicked the kernel.

## Decision: check-then-access, page by page, in one module
`userprog/umem.c` is the **only** code that may touch user memory on behalf of a system call.

| Function | Contract |
|---|---|
| `validate_range (uaddr, size, write)` | true iff **every page** of `[uaddr, uaddr+size)` is below `PHYS_BASE`, mapped in the current page directory (`pagedir_get_page`), and **writable** if `write` (new `pagedir_is_writable`). Rejects ranges that wrap around. `size == 0` is valid (nothing is touched) |
| `validate_string (ustr, max)` | scans byte by byte for the NUL and checks each page the scan **enters**. Returns the length, `UMEM_FAULT` (bad memory) or `UMEM_TOO_LONG` (no NUL within `max` bytes) |
| `copy_in (kdst, usrc, n)` / `copy_out (udst, ksrc, n)` | validate the whole range, then `memcpy` |
| `copy_in_string (kdst, ustr, size)` | `validate_string`, then copy into a kernel buffer |

**How system calls use it** (`syscall.c`):
- The syscall number and **every 4-byte argument** go through `copy_in` (`syscall_arg (f, i)`), so a
  slot that straddles a page boundary or sits above `PHYS_BASE` is caught.
- **Paths and command lines are copied into a kernel page** before use. The kernel then works only on its
  own copy, so there is no time-of-check/time-of-use gap.
- **Read/write buffers are validated in full** (every page; writable for `read`) and then accessed
  directly. Pintos user processes are single-threaded, and before VM a mapped page can't disappear
  during the call, so the check stays true for the whole access. Under VM (ADR-003) the same function
  also faults each page in and **pins** it until the call returns.
- Any validation failure ends the process with exit status −1. A path that is merely too long is an
  ordinary error (`create` returns false, `open` returns −1), not a kill.

**Defence in depth** (`exception.c`): if a user process's system call still faults on a *user* address
(it shouldn't), the fault handler ends that process with −1 instead of panicking, and releases
`filesys_lock` if it held it. A kernel-mode fault on a *kernel* address is still a panic, because that
one really is a kernel bug.

## Alternatives considered
- **Fault-tolerant copy (`get_user`/`put_user`, Pintos doc §3.1.5):** the first access faults and the
  handler returns an error code. It's faster (no page-table walk), but it doesn't *validate against the
  page tables*; it relies on the MMU to report the fault. The resume claim names page-table validation
  explicitly, and check-then-access keeps every decision in one auditable place, so it is the main
  mechanism. The fault handler keeps a safety net in the same spirit.
- **Bounce buffers for all reads and writes:** copying large buffers through kernel pages costs
  memory and time, and validate-then-access is already safe here (see above).

## Consequences
- Cost: one page-table lookup per page touched, plus a byte scan for strings. Negligible next to the
  file system.
- Every new system call (IPC in ADR-005, limits in ADR-004) must use these functions. The fuzzer (M4)
  enforces it: any panic means a missing check.
