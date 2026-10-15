#include "userprog/syscall.h"
#include <stdio.h>
#include <stdint.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/umem.h"

/* Serialises every call into the (non-thread-safe) file system.
   Lock-order rule (ADR-002): never block on another process or
   touch unvalidated user memory while holding it. */
struct lock filesys_lock;

/* Longest file name accepted from user programs (bytes, without the
   NUL).  Longer names fail the call (like ENAMETOOLONG); the Pintos
   file system itself allows 14. */
#define PATH_MAX_LEN 127

/* A system call handler: takes the raw 32-bit arguments and returns
   the value for the caller's eax. */
typedef uint32_t syscall_func (const uint32_t *args);

/* Ends the current process with exit status -1.  Used whenever a
   system call gets an invalid pointer or argument. */
static void NO_RETURN
kill_process (void)
{
  thread_current ()->exit_status = -1;
  thread_exit ();
}

/* Copies the user path at UPATH into KBUF (PATH_MAX_LEN + 1 bytes).
   Kills the process if UPATH is invalid memory; returns false if the
   name is too long. */
static bool
get_path (char *kbuf, const char *upath)
{
  int len = copy_in_string (kbuf, upath, PATH_MAX_LEN + 1);
  if (len == UMEM_FAULT)
    kill_process ();
  return len != UMEM_TOO_LONG;
}

/* Returns the open-file entry for FD in the current process, or
   NULL. */
static struct fd_entry *
find_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&t->fd_list); e != list_end (&t->fd_list);
       e = list_next (e))
    {
      struct fd_entry *fe = list_entry (e, struct fd_entry, elem);
      if (fe->fd == fd)
        return fe;
    }
  return NULL;
}

static uint32_t
sys_halt (const uint32_t *args UNUSED)
{
  shutdown_power_off ();
}

static uint32_t
sys_exit (const uint32_t *args)
{
  thread_current ()->exit_status = (int) args[0];
  thread_exit ();
}

static uint32_t
sys_exec (const uint32_t *args)
{
  char *cmd_line = palloc_get_page (0);
  int len;
  tid_t tid;

  if (cmd_line == NULL)
    return (uint32_t) -1;
  len = copy_in_string (cmd_line, (const char *) args[0], PGSIZE);
  if (len == UMEM_FAULT)
    {
      palloc_free_page (cmd_line);
      kill_process ();
    }
  tid = len == UMEM_TOO_LONG ? TID_ERROR : process_execute (cmd_line);
  palloc_free_page (cmd_line);
  return (uint32_t) tid;
}

static uint32_t
sys_wait (const uint32_t *args)
{
  return (uint32_t) process_wait ((tid_t) args[0]);
}

static uint32_t
sys_create (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  bool ok;

  if (!get_path (name, (const char *) args[0]))
    return false;
  lock_acquire (&filesys_lock);
  ok = filesys_create (name, (off_t) args[1]);
  lock_release (&filesys_lock);
  return ok;
}

static uint32_t
sys_remove (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  bool ok;

  if (!get_path (name, (const char *) args[0]))
    return false;
  lock_acquire (&filesys_lock);
  ok = filesys_remove (name);
  lock_release (&filesys_lock);
  return ok;
}

static uint32_t
sys_open (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  struct thread *t = thread_current ();
  struct fd_entry *fe;
  struct file *file;

  if (!get_path (name, (const char *) args[0]))
    return (uint32_t) -1;
  fe = malloc (sizeof *fe);
  if (fe == NULL)
    return (uint32_t) -1;

  lock_acquire (&filesys_lock);
  file = filesys_open (name);
  lock_release (&filesys_lock);
  if (file == NULL)
    {
      free (fe);
      return (uint32_t) -1;
    }
  fe->fd = t->next_fd++;
  fe->file = file;
  list_push_back (&t->fd_list, &fe->elem);
  return fe->fd;
}

static uint32_t
sys_filesize (const uint32_t *args)
{
  struct fd_entry *fe = find_fd ((int) args[0]);
  off_t size;

  if (fe == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  size = file_length (fe->file);
  lock_release (&filesys_lock);
  return size;
}

static uint32_t
sys_read (const uint32_t *args)
{
  int fd = (int) args[0];
  uint8_t *buffer = (uint8_t *) args[1];
  unsigned size = args[2];
  struct fd_entry *fe;
  off_t n;

  /* The kernel writes into BUFFER: every page must be writable. */
  if (!validate_range (buffer, size, true))
    kill_process ();

  if (fd == STDIN_FILENO)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        buffer[i] = input_getc ();
      return size;
    }
  fe = find_fd (fd);
  if (fe == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  n = file_read (fe->file, buffer, size);
  lock_release (&filesys_lock);
  return n;
}

static uint32_t
sys_write (const uint32_t *args)
{
  int fd = (int) args[0];
  const void *buffer = (const void *) args[1];
  unsigned size = args[2];
  struct fd_entry *fe;
  off_t n;

  if (!validate_range (buffer, size, false))
    kill_process ();

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  fe = find_fd (fd);
  if (fe == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  n = file_write (fe->file, buffer, size);
  lock_release (&filesys_lock);
  return n;
}

static uint32_t
sys_seek (const uint32_t *args)
{
  struct fd_entry *fe = find_fd ((int) args[0]);

  if (fe != NULL)
    {
      lock_acquire (&filesys_lock);
      file_seek (fe->file, (off_t) args[1]);
      lock_release (&filesys_lock);
    }
  return 0;
}

static uint32_t
sys_tell (const uint32_t *args)
{
  struct fd_entry *fe = find_fd ((int) args[0]);
  off_t pos;

  if (fe == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  pos = file_tell (fe->file);
  lock_release (&filesys_lock);
  return pos;
}

static uint32_t
sys_close (const uint32_t *args)
{
  struct fd_entry *fe = find_fd ((int) args[0]);

  if (fe != NULL)
    {
      list_remove (&fe->elem);
      lock_acquire (&filesys_lock);
      file_close (fe->file);
      lock_release (&filesys_lock);
      free (fe);
    }
  return 0;
}

/* System call table: handler and number of 32-bit arguments. */
static const struct
  {
    syscall_func *func;
    int argc;
  }
syscalls[] =
  {
    [SYS_HALT]     = { sys_halt,     0 },
    [SYS_EXIT]     = { sys_exit,     1 },
    [SYS_EXEC]     = { sys_exec,     1 },
    [SYS_WAIT]     = { sys_wait,     1 },
    [SYS_CREATE]   = { sys_create,   2 },
    [SYS_REMOVE]   = { sys_remove,   1 },
    [SYS_OPEN]     = { sys_open,     1 },
    [SYS_FILESIZE] = { sys_filesize, 1 },
    [SYS_READ]     = { sys_read,     3 },
    [SYS_WRITE]    = { sys_write,    3 },
    [SYS_SEEK]     = { sys_seek,     2 },
    [SYS_TELL]     = { sys_tell,     1 },
    [SYS_CLOSE]    = { sys_close,    1 },
  };
#define SYSCALL_CNT (sizeof syscalls / sizeof *syscalls)

/* Entry point for `int $0x30'.  The user stack holds the system call
   number followed by its arguments; every one of those words is
   copied in through umem (ADR-001) before use. */
static void
syscall_handler (struct intr_frame *f)
{
  uintptr_t usp = (uintptr_t) f->esp;
  uint32_t number;
  uint32_t args[3];
  int i;

  if (!copy_in (&number, (const void *) usp, sizeof number)
      || number >= SYSCALL_CNT || syscalls[number].func == NULL)
    kill_process ();
  for (i = 0; i < syscalls[number].argc; i++)
    if (!copy_in (&args[i], (const void *) (usp + 4 * (i + 1)),
                  sizeof args[i]))
      kill_process ();

  f->eax = syscalls[number].func (args);
}

/* Registers the system call interrupt handler. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}
