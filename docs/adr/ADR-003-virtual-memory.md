# ADR-003: Demand-paged virtual memory

**Status:** accepted (M6) · **Code:** `src/vm/{page,frame,swap,mmap}.[ch]`, `userprog/process.c`,
`userprog/exception.c`, `userprog/syscall.c`, `userprog/umem.c` · **Claim:** C9 · **Result:** vm 113/113

## Context
The course kernel loaded each program's pages eagerly at `exec` and gave every process a single stack
page, so a program's working set could never exceed physical RAM and the stack could not grow. Real
systems run programs far larger than RAM by paging on demand and evicting cold pages. This is the same
mechanism vLLM's PagedAttention applies to a GPU KV cache.

## Decision

### Four modules
| Module | Responsibility |
|---|---|
| `vm/page.c` | **Supplemental page table (SPT)**: one hash per process (`struct spt_entry` keyed by user page) recording where each page's data lives and how to fetch it. Lazy loading, the fault handler, stack growth, eviction write-out, and teardown. |
| `vm/frame.c` | **Frame table**: every physical user frame, with its owning page, a pin flag, and a **clock (second-chance)** eviction hand. |
| `vm/swap.c` | **Swap**: the `BLOCK_SWAP` device as page-sized slots tracked by a bitmap. |
| `vm/mmap.c` | **Memory-mapped files**: per-process regions of file-backed pages, written back when dirty. |

### A page's state, and the key correctness rule
Each `spt_entry` records a **file backing** (file, offset, read_bytes; NULL for anonymous), plus two
flags that track where the *current* data is:
- `has_swap` — the data is in swap slot `swap_slot`.
- `dirtied` — **sticky**: the page has held written data, so it can never again be reconstructed from
  its file or by zero-filling; it must always be preserved in swap.

Loading a page: if `has_swap`, swap it in; else if it has a file backing, read `read_bytes` from the
file and zero the rest; else zero-fill (fresh anonymous page).

Evicting a page (clock victim): read the dirty bit, clear the user mapping (so the next access faults),
then:
- **mmap page:** write back to its file if dirty; keep the file backing.
- **dirty, or ever-dirtied (`dirtied`):** swap it out and set `has_swap`. Set `dirtied` so it is always
  swapped from now on.
- **otherwise (clean):** drop it — it will be re-read from its file (code, read-only or unmodified data)
  or re-zeroed (untouched anonymous page) on the next fault.

The `dirtied` flag fixes the subtle bug that a naive design hits: after a written anonymous page is
swapped out and later swapped back in, its only copy of the data is in RAM. If it were then treated as a
"clean zero page" and dropped on the next eviction, the data would be lost. `dirtied` makes such a page
swap every time. (Found and fixed during M6; the symptom was `page-linear` reporting `byte 0 != 0x5a`.)

### Page fault handler
`exception.c` sends a not-present fault from a user process to `page_fault_handler(fault_addr, esp,
write)`: look the page up in the SPT and load it; or, if there is no entry and the address is within 32
bytes of the user stack pointer and inside the 8 MB stack limit, grow the stack with a fresh zero page;
otherwise the access is invalid and the process is killed. For a fault taken in the kernel during a
system call, `esp` is the user stack pointer saved on syscall entry (`thread.user_esp`).

### The deadlock, and how it is avoided
A system call such as `read` holds `filesys_lock` while copying into the user's buffer. If a buffer page
is not resident, the write faults and the fault handler must read it from a file or swap — needing
`filesys_lock` again → deadlock. Avoided by **pinning**: before taking `filesys_lock`, `read`/`write`
call `page_pin_range`, which faults every buffer page in and pins its frame; the clock never evicts a
pinned frame. After the I/O the pages are unpinned. `umem`'s `copy_in`/`copy_out`/`copy_in_string` pin
the same way for arguments, paths and command lines. **Lock order:** a thread holding `frame_lock` never
takes `filesys_lock` or `swap_lock`.

### Cleanup order
`process_exit` runs `mmap_cleanup` (write back dirty mappings) and `spt_destroy` (free frames, clear
PTEs, release swap slots) **before** `pagedir_destroy`, so the two never double-free a physical page.

### Connections to earlier milestones
- **M2 (ADR-001):** `umem` validates a range by bringing its pages in and pinning them, so the resume's
  "page-table validation of every user pointer" now means *pinned, resident, and checked* for the whole
  system call — and the read-into-unloaded-buffer deadlock is impossible by construction.
- **M5 (ADR-004):** `LIMIT_PAGES` is charged when an SPT entry is created (the process's virtual page
  footprint) and returned when it is freed, so a memory hog that grows its stack or `mmap`s is capped
  (`tests/limits/mem-hog`).

## Alternatives considered
- **Eager loading (status quo):** simplest, but cannot exceed RAM or grow the stack — the whole point.
- **Fault-based user access (`get_user`/`put_user`) instead of pinning:** would reintroduce the
  filesys-lock deadlock for buffered I/O; pinning is the standard fix.
- **FIFO or random eviction:** kept as compile-time options for the M7 benchmark; clock is the default
  because it approximates LRU cheaply.

## Consequences
- Every user frame now flows through the frame table; `frame_alloc` is the single choke point for the
  page limit and eviction.
- The VM suite passes 113/113 and the fuzzer still makes zero kernel panics. M7 measures the eviction
  policy trade-offs these modules make possible.
