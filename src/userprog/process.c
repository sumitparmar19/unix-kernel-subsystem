#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Forward declarations. */
static thread_func start_process NO_RETURN;
static bool load (const char *file_name, void (**eip) (void), void **esp);

/* ELF executable header types. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* ELF header. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header. */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type. */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

/* Flag bits for p_flags. */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Helper prototypes. */
static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *phdr, struct file *file);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
static bool push_args (const char *cmd_line, void **esp);
static bool install_page (void *upage, void *kpage, bool writable);

/* Arguments handed from process_execute() to the new child. */
struct exec_args
  {
    char *cmd_line;               /* Page holding the command line. */
    struct child_record *record;  /* Shared parent/child record. */
    struct ofile *stdin_of;       /* Becomes the child's fd 0. */
    struct ofile *stdout_of;      /* Becomes the child's fd 1. */
  };

/* Drops one reference to REC and frees it when none are left.
   Parent and child each call this exactly once (ADR-002). */
static void
child_record_release (struct child_record *rec)
{
  enum intr_level old_level = intr_disable ();
  bool last = --rec->refs == 0;
  intr_set_level (old_level);
  if (last)
    free (rec);
}

/* Starts a new process running the program in CMD_LINE (program
   name followed by arguments), with the console as its standard
   input and output.  Waits until the child has loaded its
   executable, and returns the child's tid, or TID_ERROR if the
   process could not be created or the program could not be
   loaded. */
tid_t
process_execute (const char *cmd_line)
{
  return process_execute_redirect (cmd_line, ofile_console_in (),
                                   ofile_console_out ());
}

/* Like process_execute(), but the child's fd 0 and fd 1 are STDIN_OF
   and STDOUT_OF (ADR-005).  Takes over one reference to each: they
   pass to the child, or are dropped here if no child is created. */
tid_t
process_execute_redirect (const char *cmd_line, struct ofile *stdin_of,
                          struct ofile *stdout_of)
{
  char thread_name[16];
  struct exec_args *args;
  struct child_record *rec;
  char *cmd_copy;
  size_t i;
  tid_t tid;

  /* Copy CMD_LINE: the caller's buffer may go away before the child
     reads it. */
  cmd_copy = palloc_get_page (0);
  args = malloc (sizeof *args);
  rec = malloc (sizeof *rec);
  if (cmd_copy == NULL || args == NULL || rec == NULL)
    goto fail;
  strlcpy (cmd_copy, cmd_line, PGSIZE);

  /* The thread is named after the program (first word). */
  while (*cmd_line == ' ')
    cmd_line++;
  for (i = 0; i + 1 < sizeof thread_name && cmd_line[i] != '\0'
              && cmd_line[i] != ' '; i++)
    thread_name[i] = cmd_line[i];
  thread_name[i] = '\0';

  /* The record exists before the child does, so the child can never
     run unlinked from its parent. */
  rec->exit_status = -1;
  rec->load_ok = false;
  rec->refs = 2;
  sema_init (&rec->load_sema, 0);
  sema_init (&rec->wait_sema, 0);
  args->cmd_line = cmd_copy;
  args->record = rec;
  args->stdin_of = stdin_of;
  args->stdout_of = stdout_of;

  tid = thread_create (thread_name, PRI_DEFAULT, start_process, args);
  if (tid == TID_ERROR)
    goto fail;
  rec->tid = tid;

  /* Wait for the child to report whether its program loaded.  From
     here on, the child owns ARGS and CMD_COPY. */
  sema_down (&rec->load_sema);
  if (!rec->load_ok)
    {
      child_record_release (rec);
      return TID_ERROR;
    }
  list_push_back (&thread_current ()->children, &rec->elem);
  return tid;

 fail:
  palloc_free_page (cmd_copy);
  free (args);
  free (rec);
  ofile_unref (stdin_of);
  ofile_unref (stdout_of);
  return TID_ERROR;
}

/* Thread function of a new process: loads the user program and
   starts running it. */
static void
start_process (void *args_)
{
  struct exec_args *args = args_;
  struct thread *cur = thread_current ();
  struct intr_frame if_;
  bool success;

  cur->is_user = true;
  cur->record = args->record;

  /* Install fd 0 and fd 1 before loading, so a failed load releases
     them through the normal exit path. */
  success = fdtable_init (&cur->fds, args->stdin_of, args->stdout_of);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = success && load (args->cmd_line, &if_.eip, &if_.esp);
  palloc_free_page (args->cmd_line);
  free (args);

  /* Tell the parent, which is blocked in process_execute(). */
  cur->record->load_ok = success;
  sema_up (&cur->record->load_sema);
  if (!success)
    thread_exit ();             /* exit_status stays -1. */

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in threads/intr-stubs.S). */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for child process CHILD_TID to die and returns its exit
   status.  Returns -1 if CHILD_TID is not a child of the calling
   process, or if it was already waited for. */
int
process_wait (tid_t child_tid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
    {
      struct child_record *rec = list_entry (e, struct child_record, elem);
      if (rec->tid == child_tid)
        {
          int status;

          sema_down (&rec->wait_sema);
          status = rec->exit_status;
          list_remove (&rec->elem);     /* A second wait returns -1. */
          child_record_release (rec);
          return status;
        }
    }
  return -1;
}

/* Frees the current process's resources.  Order matters (ADR-002):
   print the exit line, release files and memory, and only then wake
   a waiting parent, so the parent sees everything already freed. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if (cur->is_user)
    printf ("%s: exit(%d)\n", cur->name, cur->exit_status);

  /* Close every descriptor and the (write-denied) executable. */
  fdtable_destroy (&cur->fds);
  if (cur->exec_file != NULL)
    {
      lock_acquire (&filesys_lock);
      file_close (cur->exec_file);      /* Re-allows writes. */
      lock_release (&filesys_lock);
      cur->exec_file = NULL;
    }

  /* Orphan our children: drop our reference to each record.  A
     child still running keeps its own reference. */
  while (!list_empty (&cur->children))
    child_record_release (list_entry (list_pop_front (&cur->children),
                                      struct child_record, elem));

  /* Destroy the page directory.  Set cur->pagedir to NULL before
     switching back to the kernel-only page directory, so a timer
     interrupt can't switch back to the process page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* Report to the parent last. */
  if (cur->record != NULL)
    {
      cur->record->exit_status = cur->exit_status;
      sema_up (&cur->record->wait_sema);
      child_record_release (cur->record);
      cur->record = NULL;
    }
}

/* Sets up the CPU for running user code in the current thread. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing interrupts. */
  tss_update ();
}

/* Push command-line arguments onto the user stack. */
static bool
push_args (const char *cmd_line, void **esp)
{
  char *cmd_copy, *token, *save_ptr;
  char *argv[128];
  char *arg_addrs[128];
  int argc = 0;
  int i;

  cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
    return false;
  strlcpy (cmd_copy, cmd_line, PGSIZE);

  /* Tokenize into argv[]. */
  for (token = strtok_r (cmd_copy, " ", &save_ptr);
       token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    {
      argv[argc++] = token;
      if (argc >= 128)
        break;
    }

  /* Push argument strings in reverse order. */
  for (i = argc - 1; i >= 0; i--)
    {
      size_t len = strlen (argv[i]) + 1;  /* include '\0' */
      *esp -= len;
      memcpy (*esp, argv[i], len);
      arg_addrs[i] = *esp;
    }

  /* Align stack to 4 bytes. */
  uintptr_t esp_val = (uintptr_t)*esp;
  esp_val &= 0xfffffffc;
  *esp = (void *)esp_val;

  /* Push argv[argc] = NULL. */
  *esp -= sizeof (char *);
  *(char **)*esp = NULL;

  /* Push argv[i] pointers. */
  for (i = argc - 1; i >= 0; i--)
    {
      *esp -= sizeof (char *);
      *(char **)*esp = arg_addrs[i];
    }

  /* Save argv base. */
  char **argv_start = *esp;

  /* Push argv. */
  *esp -= sizeof (char **);
  *(char *** )*esp = argv_start;

  /* Push argc. */
  *esp -= sizeof (int);
  *(int *)*esp = argc;

  /* Fake return address. */
  *esp -= sizeof (void *);
  *(void **)*esp = NULL;

  palloc_free_page (cmd_copy);
  return true;
}

/* Loads an ELF executable from FILE_NAME into the current thread. */
static bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* The base file system is not thread-safe: hold filesys_lock for
     every file access while loading (ADR-002). */
  lock_acquire (&filesys_lock);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Extract program name for filesys_open. */
  char *cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
    goto done;
  strlcpy (cmd_copy, file_name, PGSIZE);
  char *save_ptr;
  char *prog_name = strtok_r (cmd_copy, " ", &save_ptr);

  file = filesys_open (prog_name);
  palloc_free_page (cmd_copy);

  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      file_seek (file, file_ofs);
      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        {
          printf ("load: %s: error reading program header\n", file_name);
          goto done;
        }
      file_ofs += sizeof phdr;

      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          /* Unsupported. */
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page  = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;

              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz,
                                          PGSIZE) - read_bytes);
                }
              else
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz,
                                          PGSIZE);
                }

              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Push command-line arguments. */
  if (!push_args (file_name, esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  if (success)
    {
      /* Keep the executable open and unwritable while it runs;
         process_exit() closes it, which re-allows writes. */
      file_deny_write (file);
      t->exec_file = file;
    }
  else if (file != NULL)
    file_close (file);
  lock_release (&filesys_lock);
  return success;
}

/* Checks whether PHDR describes a valid, loadable segment of FILE. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  return true;
}

/* Loads a segment starting at offset OFS in FILE at address UPAGE. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of user
   virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel virtual address
   KPAGE to the page table. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that UPAGE is not already mapped. */
  if (pagedir_get_page (t->pagedir, upage) != NULL)
    return false;

  /* Map it. */
  return pagedir_set_page (t->pagedir, upage, kpage, writable);
}

