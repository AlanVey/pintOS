#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <debug.h>

//the file indexing starts from the following value
#define FILE_DESCRIPTOR_INDEX_BASE 2
// In pintos every process only has one thread can be treated the same
typedef tid_t pid_t;
// lock for the file system
static struct lock lo_file_system;
// global access to stack pointer to make function declarations easier
static void *esp;

static void syscall_handler (struct intr_frame *);
/* Functions for individual system calls */
static void halt (void);
static void exit (int status);
static pid_t exec (const char *cmd_line);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
/*
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
*/

/* Function for reading data at specified *uaddr */
static int get_user (const uint8_t *uaddr);
/* Function for writing data (byte) at specified address (*uaddr) */
// static bool put_user (uint8_t *udst, uint8_t byte);
/* Function for String verification */
static void valid_string(const char* str);
/* Function to verify user address pointers */
static void valid_args_pointers(uint32_t* esp, int num_args);
/* Function to exit process with an error */
static void exit_with_error(int status);
//returnes a new file descriptor
static unsigned generate_file_descriptor(void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  // initilize lock used for the file system
  lock_init(&lo_file_system);
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* Checks the user address pointer is valid */
  esp = f->esp;
  uint32_t syscall = *(uint32_t*)esp;
  if(esp >= PHYS_BASE || get_user(esp) == -1)
    exit_with_error(-1);

  /* SYSTEM CALLS Implementation */
  switch(syscall)
  {
    case SYS_HALT: 
    {
      halt();
      break;
    }
    case SYS_EXIT: 
    {
      int status = *(int*)(esp + 1);
      exit(status);
      break;
    }
    case SYS_EXEC: 
    {
      const char* cmd_line = *(char**)(esp + 1);
      f->eax = exec(cmd_line);
      break;
    }
    case SYS_WAIT: 
    {
      pid_t pid = *(pid_t*)(esp + 1);
      f->eax = wait(pid);
      break;   
    }                        
    case SYS_CREATE: 
    {
      const char* file = *(char**)(esp + 1);
      unsigned initial_size = *(unsigned*)(esp + 2);
      f->eax = create(file, initial_size);
      break;
    }
    case SYS_REMOVE: 
    {
      const char* file = *(char**)(esp + 1);
      f->eax = remove(file);
      break;
    }
    case SYS_OPEN: 
    {
      const char *file = *(char**)(esp + 1);
      bool ret = open(file);
      f->eax = ret;
      if(ret == -1)
        exit_with_error(ret);
      break;
    }
    case SYS_CLOSE: 
    {
      break;
    }
    case SYS_FILESIZE: 
    {
      break;
    }
    case SYS_READ: 
    {
      break;
    }
    case SYS_WRITE: 
    {
      break;
    }
    case SYS_SEEK: 
    {
      break;
    }
    case SYS_TELL: 
    {
      break;
    }
    default: exit_with_error(-1);
  }
}

//==========================================================================//
//==========================================================================//
// System Call Functions
//==========================================================================//
//==========================================================================//
static void halt (void)
{
  shutdown();
  NOT_REACHED();
}
static void exit (int status)
{
  valid_args_pointers(esp, 1);
  exit_with_error(status);
}
static pid_t exec (const char *cmd_line)
{
  valid_args_pointers(esp, 1);
  valid_string(cmd_line);
  return process_execute(cmd_line);
}
static int wait (pid_t pid)
{
  valid_args_pointers(esp, 1);
  return process_wait(pid);
}
static bool create (const char *file, unsigned initial_size)
{
  bool ret;
  valid_args_pointers(esp, 2);
  valid_string(file);
  lock_acquire(&lo_file_system);
  ret = filesys_create(file, initial_size);
  lock_release(&lo_file_system);
  return ret;
}
static bool remove (const char *file)
{
  bool ret;
  valid_args_pointers(esp, 1);
  valid_string(file);
  lock_acquire(&lo_file_system);
  ret = filesys_remove(file);
  lock_release(&lo_file_system);
  return ret;
}
static int open (const char *file)
{
  valid_args_pointers(esp, 1);
  lock_acquire(&lo_file_system);
  struct file *f = filesys_open(file);
  if(!valid_string(file))
  {
    lock_release(&lo_file_system);
    return -1;
  }
  //creates new file_index which will be stored by reference in l_files_opened
  struct file_index *fi = malloc(sizeof(struct file_index));
  fi->i_fd = generate_file_descriptor();
  fi->f = f;
  list_push_front(&thread_current()->proc_mask.l_files_opened, &fi->le_file);
  lock_release(&lo_file_system);
  return 0;
}
static int filesize (int fd)
{

}
static int read (int fd, void *buffer, unsigned size)
{

}
static int write (int fd, const void *buffer, unsigned size)
{

}

//==========================================================================//
//==========================================================================//
// Useful utility functions
//==========================================================================//
//==========================================================================//
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault occurred. */
static int get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  return result;
}
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. 
static bool put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
} */
/* Checks a Sting and assures it is \0 terminated and no error value */
static void valid_string(const char* str)
{
  char c;
  int i = 0;
  for(; (c = (char)*(str + i)) != '\0'; i++)
  {
    if(c == -1)
      exit_with_error(-1);
  }
}
/* Assures user address pointer + offset is in user space.
   Cleans up resources if not and kills process. */
static void valid_args_pointers(uint32_t* esp, int num_args)
{
  if(esp + num_args >= (int)PHYS_BASE)
    exit_with_error(-1);
}

/* Terminates the process with exit code -1 */
static void exit_with_error(int status)
{
  thread_current()->exit_value = status;
  thread_exit();
  NOT_REACHED();
}

//returnes a new file descriptor
static unsigned generate_file_descriptor(void)
{
  unsigned stat_u_file_index = FILE_DESCRIPTOR_INDEX_BASE;
  return ++stat_u_file_index;
}
