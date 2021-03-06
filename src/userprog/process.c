#include "userprog/process.h"
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
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#define LAST_TWO_BITS_ZERO 0xfffffffc
/* Size of a page */
#define MAX_BYTES_FOR_ARGS 4096

typedef tid_t pid_t;

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool initialise_program_stack (void **esp, char *token, char **saveptr);
/* Function indirectly used as part of SYS_WAIT implementation */
static struct thread* get_child(struct list* l, pid_t pid);


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *input_from_cmd_line) 
{
  /* To store just the file name. 
     200 is an arbritrary maximum file name size, can be changed later */
  char *fn_extract;
  char *fn_with_args_copy;
  tid_t tid;
  /* Used by strtok_r, although is ignored on the first call */
  char *saveptr;
  /* struct thread *t; */


  /* Make a copy of INPUT_FROM_CMD_LINE.
     Otherwise there's a race between the caller and load() */
  fn_with_args_copy = palloc_get_page (0);
  if (fn_with_args_copy == NULL)
  {
    return TID_ERROR;
  }
  strlcpy (fn_with_args_copy, input_from_cmd_line, PGSIZE);


  /* Reserve space to store the file name */
  fn_extract = palloc_get_page (0);
  if (fn_extract == NULL) 
  {
    palloc_free_page (fn_with_args_copy);
    return TID_ERROR;
  }

  /* Copy the contents of INPUT_FROM_COMD_LINE into FN_EXTRACT
     so it can then be tokenised */
  strlcpy (fn_extract, input_from_cmd_line, PGSIZE);
  
  /* Replace INPUT_FROM_CMD_LINE with just the file name of the
     program to be executed */
  input_from_cmd_line = strtok_r (fn_extract, " ", &saveptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (input_from_cmd_line, PRI_DEFAULT, start_process, fn_with_args_copy);

  palloc_free_page (fn_extract);

  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_with_args_copy); 
  }
  return tid;
}


/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  /* To store each token from strtok_r */
  char *token;
  /* For the strtok_r function to keep track when tokenising */
  char *saveptr;


  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  /* Extract the actual file name from the string */
  token = strtok_r (file_name, " ", &saveptr);
  /* Load the ELF executable */
  success = load (token, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (!success) 
  {
    palloc_free_page (file_name);
    thread_current()->exit_value = -1;
    thread_exit ();
  }
  else 
  {
    initialise_program_stack (&if_.esp, token, &saveptr);
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}
/* Called from start_process(). Sets up the program stack as described in the spec 
   Returns true if sucessful, false if the stack isn't large enough to
   accomodate for the arguments */
static bool
initialise_program_stack (void **esp, char *token, char **saveptr)
{
  void *stack_ptr = *esp;
  int argc = 0, arg_pointers_on_stack = 0;
  /* Used in helping push the pointers for the args to the stack */
  char *stack_ptr_2;
  char **argv;
  int *argc_ptr;

  /* Push the file name and each arg onto the stack */
  do 
  {
    /* Strlen does not count the null terminator */
    size_t arg_size = strlen (token) + 1;
    /* Move the stack pointer down by the size of the argument */
    stack_ptr = (void*)(((char*)stack_ptr) - arg_size);
    /* Check to make sure there is no overflow */
    if(PHYS_BASE - stack_ptr > MAX_BYTES_FOR_ARGS)
    {
      return false;
    }
    /* Push the argument onto the stack */
    strlcpy ((char*)stack_ptr, token, arg_size);
    argc++;

    /* Get the next token, NULL is passed as the string as
       The position to continue from is saved in saveptr */
    token = strtok_r (NULL, " ", saveptr);
  }
  while (token != NULL);

  /* At this point both stack_ptr and stack_ptr_2 are pointing at 
     the bottom of the stack (As the stack is growing downwards) */
  stack_ptr_2 = (char*)stack_ptr;

  /* Rounds the stack pointer down to a multiple of 4 by doing a bitwise 
     AND of the stack pointer and a number with the last two bits, equating to
     the number 4, set as zero */
  stack_ptr = (void*)(((intptr_t)stack_ptr) & LAST_TWO_BITS_ZERO);

  /* Push a null sentinal to the stack - this ensures that argv[argc] is a null
     pointer as required by the C standard */
  stack_ptr = (((char**)stack_ptr) -1);
  *((char*)stack_ptr) = 0;

  /* Push the addresses of each of the arguments onto the stack */
  while (arg_pointers_on_stack < argc)
  {
    /* Makes sure stack_ptr_2 is pointing to the next argument */
    while(*(stack_ptr_2 - 1) != '\0')
    {
      stack_ptr_2++;
    }
    /* Now the stack pointer is pointing to a pointer to a char at the next
       available place in the stack */
    stack_ptr = (((char**)stack_ptr) - 1);
    /* Set the value of this to the next arg which is stored in stack_ptr_2 */
    *((char**)stack_ptr) = stack_ptr_2;
    /* Increment variables for next iteration */
    stack_ptr_2++;
    arg_pointers_on_stack++;
  }

  /* A pointer to the first arg */
  argv = (char**)stack_ptr;
  /* Make space for argv */
  stack_ptr = ((char**)stack_ptr - 1);
  /* The stack pointer here is a pointer to a pointer to a pointer 
     to the first arg */
  *((char***)stack_ptr) = argv;

  /* Pushes argc to the stack */
  argc_ptr = (int*)stack_ptr;
  argc_ptr--;
  *argc_ptr = argc;
  stack_ptr = (void*)argc_ptr;

  /* Pushes another null sentinel as the "return address" */
  stack_ptr = ((void**)stack_ptr - 1);
  *((void**)stack_ptr) = 0;

  *esp = stack_ptr;

  return true;
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int 
process_wait (pid_t child_pid) 
{
  int ret;
  struct thread* t = thread_current();
  struct thread* child = get_child(&(t->children), child_pid);

  if(child == NULL || child->waited)
    return -1;
  if(child->exited)
    return child->exit_value;

  ret = child->exit_value;

  child->waited = true;
  return ret;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  // Makes sure parent process is no longer waiting on it
  cur->exited = true; 

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
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

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
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

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
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

  /* Open executable file. */
  file = filesys_open (file_name);
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

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
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
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
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

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
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

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
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
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
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
        *esp = PHYS_BASE - 12;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

//extracts the process  which executes the current threads then, searches in
//its list of acquired files the one referenced by the current file descriptor
static struct thread* get_child(struct list* l, pid_t pid)
{
  struct list_elem* el;

  for(el = list_begin(l); el != list_end(l); el = list_next(el))
  {
    struct thread* child = list_entry(el, struct thread, elem);
    if(child->tid == pid)
      return child;
  }

  return NULL;
}
