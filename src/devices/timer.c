#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"


#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

//true if list of sleeping threads is uninitialized
bool b_initialized_sleeping_list = false;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);
//looks in the list of sleeping threads and wakes up the threads
//which should finish their sleep
static void fu_check_sleeping(void);
//comparison function for l_sleeping_threads
static bool fu_compare(const struct list_elem *le_a,
                       const struct list_elem *le_b, void *aux UNUSED);

/*list which holds references to threads which are asleep*/
/*orders the threads in increasing order of time when they have to be awaken*/
struct list l_sleeping_threads;

/*the structure which holds a list_elem*/
/*it also holds a thread and the time when the thread has to be awaken*/
struct wake_up
{
  //I need to store the list element by its value so that its
  //shall be initialized
  struct list_elem le;
  struct thread *th;
  int64_t wake_time;
};

//lock used by the timer_sleep function
struct lock lock_timer_sleep;


/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  /*initialize list of sleeping threads*/
  list_init(&l_sleeping_threads);
  b_initialized_sleeping_list = true;
  //initialize lock
  lock_init(&lock_timer_sleep);

  //TODO
  //I have no clue what these two do so it's safer to place the list
  //initialization before them
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks) 
{
  ASSERT (intr_get_level () == INTR_ON);

  //the thread asks to get the lock
  lock_acquire(&lock_timer_sleep);

  //creates a container holding a thread and the time this has to wake up
  struct wake_up *p_wu = malloc(sizeof(struct wake_up));
  //TODO consider moving the element in the thread itself

  p_wu->th = thread_current();
  //the wake up time for the thread is the current time + the time the
  //thread has to sleep
  //TODO what if there  is an interrupt between the calling og this function
  //and the time I access timer_ticks()?
  //if it is during the function call then I have nothing to do
  //adding a field to thread won't change anything
  p_wu->wake_time = timer_ticks() + ticks;

  //adds a list element to the end of the list
  list_insert_ordered(&l_sleeping_threads, &p_wu->le, &fu_compare, NULL);

  //releases lock
  lock_release(&lock_timer_sleep);

  intr_disable();
  //I block the current thread
  thread_block();
  intr_enable();
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;

  //TODO wtf does this do?
  thread_tick ();
  //TODO not sure if this is the proper order
  //checks if any thread can be waken up
  if(b_initialized_sleeping_list)
  {
    fu_check_sleeping();
  }
}
/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}

static void
fu_check_sleeping(void)
{
  while(!list_empty(&l_sleeping_threads))
  {
    //checks if the head of the list can be awaken
    struct wake_up *wu = list_entry(list_front(&l_sleeping_threads),
                                    struct wake_up, le);

    //check the validity of the structure extracted
    ASSERT(wu != NULL && wu->th != NULL);
    //is the wake up time is before the current time or now
    if(wu->wake_time <= timer_ticks())
    {
      //I remove the first element, but I don't need to store the pointer
      list_pop_front(&l_sleeping_threads);
      //change the status of the thread which was the front of the list
      thread_unblock(wu->th);

      //TODO: free the memory
      //I free the list element and the structure containing it from memory
      /*
      intr_enable();
      free(wu);
      intr_disable();
      */
    }
    else
    {
      return;
    }
  }
  return;
}

//comparison function for sleeping threads
static bool
fu_compare(const struct list_elem *le_a, const struct list_elem *le_b, void *aux UNUSED)
{
  //checks if the elements it are not null
  ASSERT(le_a != NULL);
  ASSERT(le_b != NULL);
  //checks its final argument does not mean anything
  ASSERT(aux == NULL);
  //obtain the struct which encompasses the element a and b
  struct wake_up *p_wu_a = list_entry(le_a, struct wake_up, le);
  struct wake_up *p_wu_b = list_entry(le_b, struct wake_up, le);

  //returns true when the time element a has to be waken up is larger so the
  //two elements have to be swaped
  return (p_wu_a->wake_time < p_wu_b->wake_time);
}
