# Phase 1 report: Unix-style OS kernel (Pintos)

**Status: in progress, blocked on the source code.**
Sections 1 to 3 are done and measured. Sections 4 to 7 need Sumit's `src/` folder, which has not
been uploaded yet. Nothing below is estimated or invented; every number has a file behind it.

Last updated: 2026-10-14

---

## 1. What the repository holds today

| File | Content | Finding |
|---|---|---|
| `Make.config`, `Makefile`, `Makefile.build`, `Makefile.kernel`, `Makefile.userprog` | Pintos top-level build files (the contents of a Pintos `src/` root, but no subfolders) | Stock-style build files with no student code. `Makefile.build` still says `# No virtual memory code yet.` and lists no `vm/` sources (see §5) |
| `LICENSE` | Stanford Pintos license | Identical to stock Pintos |
| `README.md` | One line, `# unix-kernel-subsystem` | Saved as **UTF-16**; Phase 2 replaces it |
| `.gitignore` | Some ignore rules | **Bug, fixed in this branch** (see below) |

Git history: 2 commits, both by `sumitparmar19`, on 2026-03-31 ("first commit" and "Add files via upload").
No kernel source has ever been committed.

### The `.gitignore` ignored every file
The first four lines were plain text, but `*.o` and `kernel.bin` had been appended in UTF-16
(typical of PowerShell `echo ... >> .gitignore`). Git read the broken fifth line as the pattern `*`:

```
$ git check-ignore -v threads/thread.c userprog/syscall.c Dockerfile
.gitignore:5:*	threads/thread.c
.gitignore:5:*	userprog/syscall.c
.gitignore:5:*	Dockerfile
```

So any `git add .` of the kernel source would have silently committed **nothing**. This may be
why the repo never received the code. This branch rewrites `.gitignore` in UTF-8 with the same
intent (`*.o`, `kernel.bin`, cscope/tags) plus Pintos build output (`build/`, disks, utils binaries).

## 2. Which Pintos is the base?

The root `Make.config` is **not** the JHU CS318 variant (compared line by line against
`jhu-cs318/pintos@ab69995`: different toolchain detection, `-O` vs `-O0`, no `GCCPREFIX`).
It matches the newer **Stanford `pintos-anon`** style: `uname -m` host detection with `gcc -m32` on
x86-64, `-march=i686`, `LDFLAGS = -z noseparate-code`, and the "Compiler not found ... Getting
Started section" warning.

**Not yet confirmed:** the exact handout. The Stanford hosts (`pintos-os.org`, `web.stanford.edu`)
are blocked from this sandbox (HTTP 403 / DNS). The most accurate baseline is the course's own
untouched starter code (see Questions). `scripts/diff_vs_stock.py` is ready to run the moment both trees are present.

## 3. Reproducible build and test run (done)

`Dockerfile` + `scripts/run-tests.sh` build each Pintos project and run `make -k check`, saving
raw output and a summary per project. Checked end to end on a stock Pintos tree
(results in `analysis/test-results/stock-baseline/`).

### How to run

```sh
docker build -t pintos .
docker run --rm -v "$PWD/analysis/test-results:/results" pintos            # every project present
docker run --rm -v "$PWD/analysis/test-results:/results" pintos userprog   # one project
```

Each `analysis/test-results/<project>/` folder gets `build.log`, `check.log`, `summary.txt`
(pass count, runtime, failing tests, tests with no verdict), `verdicts.txt`, `grade`, and every
test's `.output` / `.result` / `.errors`. The source root is auto-detected (`src/` or repo root).

### Three toolchain problems found and handled (no source edits)

| Problem | Cause | Handling |
|---|---|---|
| Kernel triple-faults right after "Pintos booting" | Ubuntu's GCC builds position-independent code (PIE) by default; Pintos predates that. Shown by A/B: `-fno-pie` boots, `-fcf-protection=none` alone does not | `scripts/gcc-no-pie.sh` wrapper adds `-fno-pie -no-pie` to 32-bit Pintos builds only |
| userprog kernel fails to link: `multiple definition of 'fs_device'` | GCC ≥ 10 defaults to `-fno-common`; stock `filesys/filesys.h` defines a variable in a header | Same wrapper adds `-fcommon` (the old default) |
| Bochs refuses to start: `'user_shortcut' is deprecated` (Bochs 2.6.11/2.7) or `'nogui' not available` (2.6) | The stock `utils/pintos` script writes an old-style bochsrc | All suites run under QEMU (`SIMULATOR=--qemu`, overridable). Threads tests normally use Bochs; under QEMU the timing-sensitive MLFQS tests may be less deterministic. Any MLFQS failure gets re-checked before it is reported as a real bug |

This suggests the course machines used a compiler without PIE-by-default (typical of RHEL-style
lab servers or an `i386-elf` cross compiler). That's worth confirming with Sumit.

### Stock baseline: what passes with zero student work

| Project | Stock passes | Why |
|---|---|---|
| threads  | **7 / 27** | 5 alarm tests pass with the stock busy-wait `timer_sleep`; `mlfqs-fair-2/20` pass because all threads have equal priority |
| userprog | 0 / 80 | no argument passing, no system calls, `process_wait` returns at once |
| vm       | 0 / 113 | same |
| filesys  | 0 / 102 (21 with no verdict) | same; extended tests can't reach their persistence step |

Totals come from the JHU-derived tree and may differ slightly from Sumit's handout.
**This matters for the resume:** threads credit is only fair for tests beyond these 7,
and a passing alarm test alone doesn't prove a sleep-without-busy-wait implementation.

## 4. Diff against stock Pintos (waiting on source)
Tool ready: `python3 scripts/diff_vs_stock.py STOCK/src SUMIT/src --out analysis/DIFF_SUMMARY.md`.
It lists added/modified/removed files, +/- lines per file, and the C functions that are new,
changed or removed. CRLF line endings and trailing whitespace are ignored. Checked on a synthetic change.

## 5. Early signal on the "Virtual Memory" claim
The only `Makefile.build` in the repo still has the stock lines
`# No virtual memory code yet.` / `#vm_SRC = vm/file.c`. In Pintos, a project 3 implementation
normally adds `vm/*.c` files (frame table, page table, swap) to `vm_SRC` here. This is **not proof
either way**: the root build files may come from the handout, not from Sumit's final tree. But it
is the first thing to check once the source arrives.

## 6. Course context (unverified)
A search result for the University at Buffalo CSE 421/521 syllabus (instructor Karthik Dantu) lists
PA1 = threads/scheduling, PA2 = system calls, PA3 = virtual memory, done in **teams of three**.
I could not open the page from the sandbox to confirm it. If Sumit's course was team-based, the
public README must name the teammates and describe shared work as shared (§7 of the instructions).

## 7. Remaining Phase 1 work (after the source arrives)
1. Import the source on a branch, with its original git history if it has one, and open a PR.
2. Diff against the exact stock handout → `analysis/DIFF_SUMMARY.md`; git history (commits, dates, authors).
3. Run every completed project in Docker → `analysis/test-results/<project>/`.
4. Design write-up with file:function references (threads / userprog / vm as present).
5. Resume claim verdicts (all 9), one measured resume bullet.
6. `analysis/INTERVIEW_PREP.md` and `analysis/PHASE2_PLAN.md`.

## Questions for Sumit
1. **Upload the source:** the full `src/` folder (or a .zip). If it's a git repo, upload it with its `.git` folder so the history is kept.
2. **Stock baseline:** do you still have the untouched course starter code (tarball or the repo you cloned at the start)? It gives an exact diff. Otherwise tell me where the course got Pintos from.
3. **Course details:** course name and number, semester, which projects were assigned (threads / userprog / vm / filesys), and which you completed.
4. **Team:** did you work with partners? If yes, their names, and who wrote which parts if you know.
5. **Course environment:** did you build on a university Linux server (which one?) or locally? Bochs or QEMU?
6. **Branch:** the instructions say `import-source`, but this session is set to push to
   `claude/serene-newton-57q2qk`. Is it OK to do the import and its PR from this branch?
7. **Layout:** the repo root currently holds `src/`-level files. I suggest importing your tree as
   `src/` (standard Pintos layout, keeps the root for README/Dockerfile/analysis) and removing the
   duplicate root Makefiles in the same PR. OK?
