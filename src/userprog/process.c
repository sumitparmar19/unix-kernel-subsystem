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

/* Starts a new thread running a user program loaded from FILE_NAME. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME. */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Extract program name for thread name. */
  char *cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  strlcpy (cmd_copy, file_name, PGSIZE);

  char *save_ptr;
  char *prog_name = strtok_r (cmd_copy, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy);

  palloc_free_page (cmd_copy);

if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy);
    return TID_ERROR;
  }

/* Create child_info and link into parent's children list. */
struct thread *cur = thread_current ();
struct child_info *ci = malloc (sizeof *ci);
if (ci != NULL)
  {
    ci->tid = tid;
    ci->exit_status = -1;
    ci->finished = false;
    ci->waited = false;
    sema_init (&ci->wait_sema, 0);
    list_push_back (&cur->children, &ci->elem);

    struct thread *child = thread_get_by_tid (tid);
    if (child != NULL)
      child->parent_info = ci;
  }

return tid;


}
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);


  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
    thread_exit ();

  /* Start the user process by simulating a return from an interrupt. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Wait for child_tid to finish and return its exit status. */
int
process_wait (tid_t child_tid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->children);
       e != list_end (&cur->children);
       e = list_next (e))
    {
      struct child_info *ci = list_entry (e, struct child_info, elem);

      if (ci->tid == child_tid)
        {
          /* Only one successful wait per child. */
          if (ci->waited)
            return -1;

          ci->waited = true;

          /* Sleep until the child actually exits. */
          if (!ci->finished)
            sema_down (&ci->wait_sema);

          /* Child is done: grab status and free record. */
          int status = ci->exit_status;
          list_remove (&ci->elem);
          free (ci);
          return status;
        }
    }

  /* child_tid is not a child of this process. */
  return -1;
}
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

#ifdef USERPROG
  /* Tell our parent (if any) that we are exiting. */
  if (cur->parent_info != NULL)
    {
      struct child_info *ci = cur->parent_info;
      ci->exit_status = cur->exit_status;   /* status set by do_exit/SYS_EXIT */
      ci->finished = true;
      sema_up (&ci->wait_sema);             /* wake parent in process_wait() */
    }

  /* Required Pintos exit line. */
  printf ("%s: exit(%d)\n", cur->name, cur->exit_status);

  /* Close all open files for this process. */
  while (!list_empty (&cur->fd_list))
    {
      struct list_elem *e = list_pop_front (&cur->fd_list);
      struct fd_entry *fd_e = list_entry (e, struct fd_entry, elem);
      lock_acquire (&filesys_lock);
      file_close (fd_e->file);
      lock_release (&filesys_lock);
      free (fd_e);
    }
#endif

  /* Destroy the page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
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
  if (file != NULL)
    file_close (file);
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

  /* The virtual memory region must be in user address space. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
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

