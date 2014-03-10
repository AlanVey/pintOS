#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <debug.h>

static void syscall_handler (struct intr_frame *);

/* Function for reading data at specified *uaddr */
static int get_user (const uint32_t *uaddr);
/* Function for writing data (byte) at specified address (*uaddr) */
static bool put_user (uint8_t *udst, uint8_t byte);
/* Function for String verification */
static void valid_string(const char* str);
/* Function to verify user address pointers */
static void valid_args_pointers(uint32_t* esp, uint8_t num_args);
/* Function to exit process with an error */
static void exit_with_error(uint32_t status);

/* Functions for individual system calls */
static void halt     (void);
static void exit     (int status) NO_RETURN;
static tid_t exec    (const char *file);
static int wait      (tid_t tid);
static bool create   (const char *file, unsigned initial_size);
static bool remove   (const char *file);
static int open      (const char *file);
static void close    (int fd);
static int filesize  (int fd);
static int read      (int fd, void *buffer, unsigned length);
static int write     (int fd, const void *buffer, unsigned length);
static void seek     (int fd, unsigned position);
static unsigned tell (int fd);

// lock for the file system
static struct lock lo_file_system;
// global acces to esp to make function declarations easier
static uint32_t *esp;

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
  if(esp >= PHYS_BASE || get_user(esp) == -1)
    exit_with_error(NULL);

  /* SYSTEM CALLS Implementation */
  switch(*esp)
  {
    case SYS_HALT     :          halt();                              break;
    case SYS_EXIT     :          exit(*(esp + 1));                    break;
    case SYS_EXEC     : f->eax = exec(*(esp + 1));                    break;
    case SYS_WAIT     : f->eax = wait(*(esp + 1));                    break;                           
    case SYS_CREATE   : f->eax = create(NULL, *(esp + 2));            break;
    case SYS_REMOVE   : f->eax = remove(NULL);                        break;
    case SYS_OPEN     : f->eax = open(NULL);                          break;
    case SYS_CLOSE    :          close(*(esp + 1));                   break;
    case SYS_FILESIZE : f->eax = filesize(*(esp + 1));                break;
    case SYS_READ     : f->eax = read(*(esp + 1), NULL, *(esp + 3));  break;
    case SYS_WRITE    : f->eax = write(*(esp + 1), NULL, *(esp + 3)); break;
    case SYS_SEEK     :          seek(*(esp + 1), *(esp + 2));        break;
    case SYS_TELL     : f->eax = tell(*(esp + 1));                    break;
    default           : exit_with_error(NULL);
  }
}

//==========================================================================//
//==========================================================================//
// System Call Functions
//==========================================================================//
//==========================================================================//
static void halt(void)
{
  shutdown();
  NOT_REACHED();
}
static void exit(int status) NO_RETURN
{
  valid_args_pointers(esp, 1);
  exit_with_error(status);
}
static tid_t exec(const char *cmd_line)
{
  valid_args_pointers(esp, 1);
  valid_string(cmd_line);
  return process_execute(cmd_line);
}
static int wait(tid_t pid)
{
  valid_args_pointers(esp, 1);
  return process_wait(pid);
}
static bool create(const char *file, unsigned initial_size)
{
  bool ret;
  valid_args_pointers(esp, 2);
  valid_string(*file);
  lock_aquire(&lo_file_system);
  ret = filesys_create(file, initial_size);
  lock_release(&lo_file_system);
  return ret;
}
static bool remove(const char *file)
{
  bool ret;
  valid_args_pointers(esp, 1);
  valid_string(*file);
  lock_aquire(&lo_file_system);
  ret = filesys_remove(file);
  lock_release(&lo_file_system);
  return ret;
}
static int open(const char *file)
{
  file = NULL;
  valid_args_pointers(esp, 1);
  return 0;
}
static void close(int fd)
{
  fd = 0;
  valid_args_pointers(esp, 1);
}
static int filesize(int fd)
{
  valid_args_pointers(esp, 1);
  return fd;
}
static int read(int fd, void *buffer, unsigned length)
{
  buffer = NULL;
  length = 0;
  valid_args_pointers(esp, 3);
  return 0;
}
static int write(int fd, const void *buffer, unsigned length)
{
  buffer = NULL;
  length = 0;
  alid_args_pointers(esp, 3);
  return fd;
}
static void seek(int fd, unsigned position)
{
  fd = fd + position;
  valid_args_pointers(esp, 2);
}
static unsigned tell(int fd)
{
  valid_args_pointers(esp, 1);
  return fd;
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
   Returns true if successful, false if a segfault occurred. */
static bool put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
/* Checks a Sting and assures it is \0 terminated and no error value */
static void valid_string(const char* str)
{
  char c;
  for(int i = 0; c = get_user(esp + i) != '\0'; i++)
  {
    if(c == -1)
      exit_with_error(NULL);
  }
}
/* Assures user address pointer + offset is in user space.
   Cleans up resources if not and kills process. */
static void valid_args_pointers(uint32_t* esp, int8_t num_arg)
{
  if(esp + num_arg >= PHYS_BASE)
    exit_with_error(NULL);
}

/* Terminates the process with exit code -1 */
static void exit_with_error(int32_t status)
{
  thread_current()->exit_value = status ? status : -1;
  thread_exit();
  NOT_REACHED();
}
