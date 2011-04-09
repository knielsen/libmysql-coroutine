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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <ucontext.h>

enum wait_status {
  WAIT_READ= 1,
  WAIT_WRITE= 2
};

struct my_state {
  /*
    Set true when we are actively running some async method, started with some
    foo_start() call.
  */
  int async_call_active;

  int ret_status;
  union {
    int r_int;
  } ret_result;
  ucontext_t caller_context;
  ucontext_t blocked_io_context;
  char *stack_mem;

  /* Passing parameters for each different async call. */
  union {
    struct {
      int param;
    } p_foo;
  } param;
};

/*
  The makecontext() only allows to pass integers into the created context :-(
  We want to pass pointers, so we do it this kinda hackish way.
  Anyway, it should work everywhere, and at least it does not break strict
  aliasing.
*/
union pass_void_ptr_as_2_int {
  int a[2];
  void *p;
};

#define STACK_SIZE 16384

static int
state_init(struct my_state *s)
{
  if (2*sizeof(int) < sizeof(void *))
  {
    fprintf(stderr,
            "Error: Unable to store pointer in 2 ints on this architecture\n");
    return -1;
  }

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

static void
my_yield(struct my_state *s, int ret_status)
{
  int err;

  if (!s->async_call_active)
  {
    fprintf(stderr, "Error: cannot yield when call not active.\n");
    return;
  }

  /*
    Switch to main context so it can return our wait status.
    When we are restarted in foo_cont(), the swapcontext() call will return 0
  */
  s->ret_status= ret_status;
  err= swapcontext(&s->blocked_io_context, &s->caller_context);
  if (err)
    fprintf(stderr, "Aieie, swapcontext() returns error: %d\n", err);
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
foo_start_internal(i1, i2)
{
  int ret;
  int err;
  union pass_void_ptr_as_2_int u;
  struct my_state *s;

  u.a[0]= i1;
  u.a[1]= i2;
  s= (struct my_state *)u.p;

  ret= foo(s, s->param.p_foo.param);
  s->async_call_active= 0;
  s->ret_result.r_int= ret;
  s->ret_status= 0;
  err= setcontext(&s->caller_context);
  fprintf(stderr, "Aieie, setcontext() failed: %d (errno=%d)\n", err, errno);
}

/*
  These are our async wrappers around foo(). They allow to use foo() in an
  asynchroneous way without having to write all of foo() in event-based style.
*/
static int
foo_start(int *ret, struct my_state *s, int param)
{
  int err;
  union pass_void_ptr_as_2_int u;

  /*
    Create a new context for the actual call, so we can suspend it in case we
    need to block on I/O.
  */
  err= getcontext(&s->blocked_io_context);
  if (err)
    return 1;
  s->blocked_io_context.uc_stack.ss_sp= s->stack_mem;
  s->blocked_io_context.uc_stack.ss_size= STACK_SIZE;
  s->blocked_io_context.uc_link= NULL;
  s->param.p_foo.param= param;
  u.p= s;
  makecontext(&s->blocked_io_context, foo_start_internal, 2, u.a[0], u.a[1]);
  s->async_call_active= 1;

  err= swapcontext(&s->caller_context, &s->blocked_io_context);
  if (err)
  {
    fprintf(stderr, "Aieie, swapcontext() failed: %d\n", err);
    *ret= -1;
    return 0;
  }

  if (!s->ret_status)
    *ret= s->ret_result.r_int;

  return s->ret_status;
}

static int
foo_cont(int *ret, struct my_state *s)
{
  int err;

  if (!s->async_call_active)
  {
    fprintf(stderr,
            "Error: calling foo_cont() without foo_start() being active\n");
    *ret= -1;
    return 0;
  }

  err= swapcontext(&s->caller_context, &s->blocked_io_context);
  if (err)
  {
    fprintf(stderr, "Aieie, swapcontext() failed: %d\n", err);
    *ret= -1;
    return 0;
  }

  if (!s->ret_status)
    *ret= s->ret_result.r_int;

  return s->ret_status;
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
