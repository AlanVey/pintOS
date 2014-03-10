#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <debug.h>

static void syscall_handler (struct intr_frame *);

/* Function for reading data at specified *uaddr */
static int get_user (const uint8_t *uaddr);
/* Function for writing data (byte) at specified address (*uaddr) */
static bool put_user (uint8_t *udst, uint8_t byte);
/* Function for String verification */
static void valid_string(char* str);
/* Function to verify user address pointers */
static void valid_args_pointers(uint32_t* esp, uint8_t num_args);
/* Function to exit process with an error */
static void exit_with_error();

// lock for the file system
static struct lock lo_file_system;

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
  uint32_t *esp = f->esp;
  if(esp >= PHYS_BASE || get_user(esp) == -1)
    exit_with_error();

  /* SYSTEM CALLS Implementation */
  if(*esp == SYS_HALT)
  {
    shutdown();
    NOT_REACHED();
  }
  else if(*esp == SYS_EXIT)
  {
    valid_args_pointers(esp, 1);
    thread_current()->exit_status = *(esp + 1);
    thread_exit();
    NOT_REACHED();
  }
  else if(*esp == SYS_EXEC)
  {
    valid_args_pointers(esp, 1);
    char* cmd_line = *(esp + 1);
    valid_string(cmd_line);
    f->eax = process_execute(cmd_line);
  }
  else if(*esp == SYS_WAIT)
  {
    valid_args_pointers(sp, 1);
    f->eax = process_wait(*(esp + 1));
  }                           
  else if(*esp == SYS_CREATE)
  {
    valid_args_pointers(sp, 2);
    char *file = *(esp + 1);
    unsigned size = *(esp + 2);

    valid_string(file);
    lock_aquire(&lo_file_system);
    f->eax = filesys_create(file, size);
    lock_release(&lo_file_system);
  }
  else if(*esp == SYS_REMOVE)
  {
    valid_args_pointers(sp, 1);
    char *file = *(esp + 1);

    valid_string(file);
    lock_aquire(&lo_file_system);
    f->eax = filesys_remove(file);
    lock_release(&lo_file_system);
  }
  else if(*esp == SYS_OPEN)
  {
    valid_args_pointers(sp, 1);
  }
  else if(*esp == SYS_CLOSE)
  {
    valid_args_pointers(sp, 1);
  }
  else if(*esp == SYS_FILESIZE)
  {
    valid_args_pointers(sp, 1);
  }
  else if(*esp == SYS_READ)
  {
    valid_args_pointers(sp, 3);
  }
  else if(*esp == SYS_WRITE)
  {
    valid_args_pointers(sp, 3);
  }
  else if(*esp == SYS_SEEK)
  {
    valid_args_pointers(sp, 2);
  }
  else if(*esp == SYS_TELL)
  {
    valid_args_pointers(sp, 1);
  }
  else
    exit_with_error();
}


//==========================================================================//
//==========================================================================//
// Useful utility functions
//==========================================================================//
//==========================================================================//
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
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
static void valid_string(char* str)
{
  char c;
  for(int i = 0; c = get_user(esp + i) != '\0'; i++)
  {
    if(c == -1)
      exit_with_error();
  }
}
/* Assures user address pointer + offset is in user space.
   Cleans up resources if not and kills process. */
static void valid_args_pointers(uint32_t* esp, int8_t num_arg)
{
  if(esp + num_arg >= PHYS_BASE)
    exit_with_error();
}

/* Terminates the process with exit code -1 */
static void exit_with_error()
{
  thread_current()->exit_status = -1;
  thread_exit();
}
