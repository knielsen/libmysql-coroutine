/*
  Copyright 2011 Kristian Nielsen

  Experiments with non-blocking libmysql.

  This is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Simple example of how one might use the swapcontext() etc. methods. */

#include <stdlib.h>
#include <stdio.h>


#include "my_context.h"

enum wait_status {
  WAIT_READ= 1,
  WAIT_WRITE= 2,
  WAIT_TIMEOUT= 4                     /* ToDo: pass timeout value somewhere */
};

#define STACK_SIZE 16384

struct my_state {
  /*
    Set true when we are actively running some async method, started with some
    foo_start() call.
  */
  int async_call_active;
  /* The wait status (WAIT_READ and/or WAIT_WRITE) when suspending. */
  int ret_status;
  /* Passing parameters for each different async call. */
  union {
    struct {
      int param;
    } p_foo;
  } param;
  /* Return value from each different async call. */
  union {
    int r_int;
  } ret_result;
  /* Memory for stack for async context. */
  char *stack_mem;

  /* Implementation-specific async context. */
  struct my_context async_context;
};

static int
state_init(struct my_state *s)
{
  s->async_call_active= 0;
  s->stack_mem= malloc(STACK_SIZE);
  if (!s->stack_mem)
    return -1;

  return 0;
}

static void
state_deinit(struct my_state *s)
{
  if (s->stack_mem)
    free(s->stack_mem);
}


/*
  This function is used to suspend an async call when we need to block for I/O.

  The ret_status tells which condition(s) the calling application must wait for
  before resuming us again with the appropriate foo_cont() method.
*/
static void
my_yield(struct my_state *s, int ret_status)
{
  int err;

  /*
    Switch to main context so it can return our wait status. When we are
    restarted in foo_cont(), the my_context_yield() call will return 0
  */
  s->ret_status= ret_status;
  err= my_context_yield(&s->async_context);
  if (err)
    fprintf(stderr, "Aieie, my_context_yield() returns error: %d\n", err);
}


/*
  This is the wrapper for our blocking call.
  In the real implementation, this might be a blocking socket call like
  read() or connect().

  It shows how to yield back to the caller when running in async mode.
*/
static int
do_out(struct my_state *s, int x)
{
  if (s->async_call_active)
  {
    /* Simulate yielding due to blocking on write. */
    my_yield(s, WAIT_WRITE);
  }
  else
  {
    /*
      We are running synchroneously, so we can block, eg. use write() on an
      fd in blocking mode or use poll() to wait.
    */
    sleep(1);
  }

  printf("%d\n", x);
    return x % 3;
}

/*
  This is our example method, the sync version that may block inside do_out().
*/
static int
foo(struct my_state *s, int param)
{
  if (param <= 0)
    return -1;                                  /* Error */

  while (param)
  {
    int res= do_out(s, param--);
    printf("Result=%d\n", res);
  }

  return 0;
}

static void
foo_start_internal(void *d)
{
  int ret;
  int err;
  struct my_state *s;

  s= (struct my_state *)d;

  ret= foo(s, s->param.p_foo.param);
  s->async_call_active= 0;
  s->ret_result.r_int= ret;
  s->ret_status= 0;                             /* 0 means we are done */
}

/*
  These are our async wrappers around foo(). They allow to use foo() in an
  asynchroneous way without having to write all of foo() in event-based style.
*/
static int
foo_start(int *ret, struct my_state *s, int param)
{
  int res;

  /* Spawn the actual call in a separate context, so we can suspend it in case
    we need to block on I/O.
  */
  s->param.p_foo.param= param;
  s->async_call_active= 1;
  res= my_context_spawn(&s->async_context, foo_start_internal, s,
                        s->stack_mem, STACK_SIZE);
  if (res < 0)
  {
    /* Error. */
    return 1;
  }
  else if (res > 0)
  {
    /* Suspended. */
    return s->ret_status;
  }
  else
  {
    /* Finished. */
    *ret= s->ret_result.r_int;
    return 0;
  }
}

static int
foo_cont(int *ret, struct my_state *s)
{
  int res;

  if (!s->async_call_active)
  {
    fprintf(stderr,
            "Error: calling foo_cont() without foo_start() being active\n");
    *ret= -1;
    return 0;
  }

  res= my_context_continue(&s->async_context);
  if (res < 0)
  {
    fprintf(stderr, "Aieie, my_context_continue() failed: %d\n", res);
    *ret= -1;
    return 0;
  }
  else if (res > 0)
  {
    /* Suspended again. */
    return s->ret_status;
  }
  else
  {
    /* Finished. */
    *ret= s->ret_result.r_int;
    return 0;
  }
}

int
main(int argc, char *argv[])
{
  struct my_state s;
  int stat;
  int ret;

  ret= state_init(&s);
  if (ret)
  {
    fprintf(stderr, "Error: failed to state_init()\n");
    exit(1);
  }

  printf("Sync:\n");
  ret= foo(&s, 4);
  printf("Sync done: %d\n", ret);

  printf("\nAsync:\n");
  stat= foo_start(&ret, &s, 4);
  while (stat)
  {
    printf("...again, stata=%d\n", stat);
    sleep(1);
    stat= foo_cont(&ret, &s);
  }
  printf("Async done: %d\n", ret);

  return 0;
}
