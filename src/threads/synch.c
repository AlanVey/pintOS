/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void sema_test_helper (void *sema_);
//a thread gives its priority to the lock
//which could modify the holder's priority
static void fu_donate_priority(struct lock *l, int i_waiter_priority);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      struct thread *t = thread_current();
      ASSERT(&t->elem != NULL);
      list_insert_ordered (&sema->waiters, &t->elem, fu_comp_priority, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  //remembers the first waiting thread
  struct thread *t_first_waiting = NULL;
  if (!list_empty (&sema->waiters))
  {
    t_first_waiting = list_entry(list_pop_front(&sema->waiters),
                                 struct thread, elem);
    thread_unblock(t_first_waiting);
  }
  sema->value++;

  //the current thread will yield in case the unblocked thread has higher priority
  //this works because sema_up works only when there isn't an interrupt context
  if(fu_necessary_to_yield())
  {
    thread_yield();
  }
  intr_set_level (old_level);
}

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  lock->i_lock_priority = 0;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  //every thread which tries to get a lock donates its priority
  //there are no querries so there should be no race problems
  fu_donate_priority(lock, thread_get_priority());
  sema_down (&lock->semaphore);
  lock->holder = thread_current();
  list_insert_ordered(&lock->holder->l_locks_held, &lock->le,
                      fu_lcomp_locks, NULL);
  //donates the running thread's own priority
  fu_donate_priority(lock, lock->holder->own_priority);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  //remember the current thread and assign it the maximum priority so that
  //there will be no race conditions between releasing the semaphore and
  //updating its priority
  struct thread *t = lock->holder;
  lock->holder = NULL;
  thread_set_priority(PRI_MAX);
  //by setting it to 0 before releasing the lock, we're making sure that
  //no donation will be lost
  lock->i_lock_priority = 0;

  sema_up (&lock->semaphore);

  //check that no other thread comes here first
  ASSERT(thread_current() == t);
  list_remove(&lock->le);

  //extract maximum
  int i_max_list_priority = -1;
  if(!list_empty(&t->l_locks_held))
  {
    i_max_list_priority = list_entry(list_front(&t->l_locks_held),
                                     struct lock, le);
    ASSERT(m_valid_priority(i_max_list_priority));
  }

  int i_new_priority = t->own_priority > i_max_list_priority ?
                       t->own_priority : i_max_list_priority ;
  thread_set_priority(i_new_priority);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  ASSERT(&waiter.elem != NULL);
  list_insert_ordered (&cond->waiters, &waiter.elem, fu_comp_priority, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

//comparison function for list which holds locks and orderes them by their 
//priority
bool
fu_lcomp_locks(const struct list_elem *a,
              const struct list_elem *b,
              void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);

  //obtains the threads which are encompassing those elements
  struct lock *l_a = list_entry(a, struct lock, le);
  struct lock *l_b = list_entry(b, struct lock, le);

  ASSERT(l_a != NULL);
  ASSERT(l_b != NULL);

  //establishes decreasing ordering
  return l_a->i_lock_priority > l_b->i_lock_priority;
}

//a thread gives its priority to the lock
//which could modify the holder's priority
static void fu_donate_priority(struct lock *l, int i_waiter_priority)
{
  ASSERT(l != NULL);
  ASSERT(m_valid_priority(i_waiter_priority));
  ASSERT(m_valid_priority(l->i_lock_priority));
  //there is a danger in case of a race
  //if the holder of the list changes, than this could add useless elements to
  //the list
  struct semaphore se;
  sema_init(&se, 1);
  sema_down(&se);
  //recursively goes through all locks in its path
  //as long as it can give a higher priority
  if(i_waiter_priority > l->i_lock_priority)
  {
    l->i_lock_priority = i_waiter_priority;
    if(l->holder != NULL)
    {
      //the lock adds itself to the holder's list
      struct thread *t = l->holder;
      ASSERT(m_valid_priority(t->priority));

      //if the lock is already in the list it gets removed
      if(list_entry(&l->le, struct lock, le) != NULL)
      {
        list_remove(&l->le);
      }
      list_insert_ordered(&t->l_locks_held, &l->le, fu_lcomp_locks, NULL);

      if(l->i_lock_priority > t->priority)
      {
        //can not be the running thread
        ASSERT(t->status == THREAD_READY ||
               t->status == THREAD_BLOCKED);
        //the lock changes the holder's priority
        t->priority = l->i_lock_priority;
        //the lock holder gets reinserted in the ready list_init
        if(t->status == THREAD_BLOCKED)
        {
          thread_unblock(t);
        }
        else
        {
          //reinsert the given thread into the ready list taking into account its
          //priority
          thread_reinsert(t);
        }
      }
    }
  }
  sema_up(&se);
  return;
}
