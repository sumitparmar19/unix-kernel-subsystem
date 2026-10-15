#include "userprog/syscall.h"
#include <stdio.h>
#include "syscall-nr.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

/* Forward declaration. */
static void syscall_handler (struct intr_frame *f);

struct lock filesys_lock;

/* Install the syscall interrupt handler. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* Kill the current user process with given status, printing exit line. */
static void
do_exit (int status)
{
  struct thread *t = thread_current ();
  t->exit_status = status;
  thread_exit ();
}

/* Validate a user pointer; kill the process if invalid. */
static void
validate_ptr (const void *uaddr)
{
  if (uaddr == NULL
      || !is_user_vaddr (uaddr)
      || pagedir_get_page (thread_current ()->pagedir, uaddr) == NULL)
    {
      do_exit (-1);
    }
}

/* Check a single user pointer.  Kill the process if invalid. */
static void
check_user_ptr (const void *uaddr)
{
  if (uaddr == NULL || !is_user_vaddr (uaddr))
    {
      thread_current ()->exit_status = -1;
      thread_exit ();
    }

#ifdef USERPROG
  void *ptr = pagedir_get_page (thread_current ()->pagedir, uaddr);
  if (ptr == NULL)
    {
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
#endif
}

/* Check a buffer (roughly).  This is simple but good enough for tests. */
static void
check_user_buffer (const void *buffer, unsigned size)
{
  const uint8_t *b = buffer;
  check_user_ptr (b);
  if (size > 0)
    check_user_ptr (b + size - 1);
}

/* Find fd_entry by fd for current thread. */
static struct fd_entry *
find_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&t->fd_list); e != list_end (&t->fd_list);
       e = list_next (e))
    {
      struct fd_entry *f = list_entry (e, struct fd_entry, elem);
      if (f->fd == fd)
        return f;
    }
  return NULL;
}

/* Main system call handler. */
static void
syscall_handler (struct intr_frame *f)
{
  int *esp = f->esp;
  check_user_ptr (esp);

  int syscall_no = *esp;

  switch (syscall_no)
    {
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
      {
        int status = *(esp + 1);
        thread_current ()->exit_status = status;
        thread_exit ();
        break;
      }

    case SYS_EXEC:
      {
        const char *cmd_line = (const char *) *(esp + 1);
        check_user_ptr (cmd_line);
        f->eax = process_execute (cmd_line);
        break;
      }

    case SYS_WAIT:
      {
        tid_t tid = *(esp + 1);
        f->eax = process_wait (tid);
        break;
      }

    case SYS_CREATE:
      {
        const char *name = (const char *) *(esp + 1);
        unsigned size = *(esp + 2);
        check_user_ptr (name);
        lock_acquire (&filesys_lock);
        bool ok = filesys_create (name, size);
        lock_release (&filesys_lock);
        f->eax = ok;
        break;
      }

    case SYS_REMOVE:
      {
        const char *name = (const char *) *(esp + 1);
        check_user_ptr (name);
        lock_acquire (&filesys_lock);
        bool ok = filesys_remove (name);
        lock_release (&filesys_lock);
        f->eax = ok;
        break;
      }

    case SYS_OPEN:
      {
        const char *name = (const char *) *(esp + 1);
        check_user_ptr (name);

        lock_acquire (&filesys_lock);
        struct file *file = filesys_open (name);
        lock_release (&filesys_lock);

        if (file == NULL)
          {
            f->eax = -1;
          }
        else
          {
            struct thread *t = thread_current ();
            struct fd_entry *fd_e = malloc (sizeof *fd_e);
            if (fd_e == NULL)
              {
                lock_acquire (&filesys_lock);
                file_close (file);
                lock_release (&filesys_lock);
                f->eax = -1;
              }
            else
              {
                fd_e->fd = t->next_fd++;
                fd_e->file = file;
                list_push_back (&t->fd_list, &fd_e->elem);
                f->eax = fd_e->fd;
              }
          }
        break;
      }

    case SYS_FILESIZE:
      {
        int fd = *(esp + 1);
        struct fd_entry *fd_e = find_fd (fd);
        if (fd_e == NULL)
          f->eax = -1;
        else
          {
            lock_acquire (&filesys_lock);
            int size = file_length (fd_e->file);
            lock_release (&filesys_lock);
            f->eax = size;
          }
        break;
      }

    case SYS_READ:
      {
        int fd = *(esp + 1);
        void *buffer = (void *) *(esp + 2);
        unsigned size = *(esp + 3);
        check_user_buffer (buffer, size);

        if (fd == 0)
          {
            /* stdin */
            unsigned i;
            uint8_t *b = buffer;
            for (i = 0; i < size; i++)
              b[i] = input_getc ();
            f->eax = size;
          }
        else
          {
            struct fd_entry *fd_e = find_fd (fd);
            if (fd_e == NULL)
              f->eax = -1;
            else
              {
                lock_acquire (&filesys_lock);
                int read_bytes = file_read (fd_e->file, buffer, size);
                lock_release (&filesys_lock);
                f->eax = read_bytes;
              }
          }
        break;
      }

    case SYS_WRITE:
      {
        int fd = *(esp + 1);
        const void *buffer = (const void *) *(esp + 2);
        unsigned size = *(esp + 3);
        check_user_buffer (buffer, size);

        if (fd == 1)
          {
            /* stdout */
            putbuf (buffer, size);
            f->eax = size;
          }
        else
          {
            struct fd_entry *fd_e = find_fd (fd);
            if (fd_e == NULL)
              f->eax = -1;
            else
              {
                lock_acquire (&filesys_lock);
                int written = file_write (fd_e->file, buffer, size);
                lock_release (&filesys_lock);
                f->eax = written;
              }
          }
        break;
      }

    case SYS_SEEK:
      {
        int fd = *(esp + 1);
        unsigned pos = *(esp + 2);
        struct fd_entry *fd_e = find_fd (fd);
        if (fd_e != NULL)
          {
            lock_acquire (&filesys_lock);
            file_seek (fd_e->file, pos);
            lock_release (&filesys_lock);
          }
        break;
      }

    case SYS_TELL:
      {
        int fd = *(esp + 1);
        struct fd_entry *fd_e = find_fd (fd);
        if (fd_e == NULL)
          f->eax = -1;
        else
          {
            lock_acquire (&filesys_lock);
            unsigned pos = file_tell (fd_e->file);
            lock_release (&filesys_lock);
            f->eax = pos;
          }
        break;
      }

    case SYS_CLOSE:
      {
        int fd = *(esp + 1);
        struct fd_entry *fd_e = find_fd (fd);
        if (fd_e != NULL)
          {
            lock_acquire (&filesys_lock);
            file_close (fd_e->file);
            lock_release (&filesys_lock);
            list_remove (&fd_e->elem);
            free (fd_e);
          }
        break;
      }

    default:
      /* Unknown syscall -> kill the process. */
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
}

