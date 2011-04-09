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

/*
  Implementation of async context spawning using Posix ucontext and
  swapcontext().
*/

#include <stdio.h>
#include <errno.h>

#include "my_context.h"

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


static void
my_context_spawn_internal(i0, i1)
{
  int err;
  struct my_context *c;
  union pass_void_ptr_as_2_int u;

  u.a[0]= i0;
  u.a[1]= i1;
  c= (struct my_context *)u.p;

  (*c->user_func)(c->user_data);
  c->active= 0;
  err= setcontext(&c->base_context);
  fprintf(stderr, "Aieie, setcontext() failed: %d (errno=%d)\n", err, errno);
}


int
my_context_continue(struct my_context *c)
{
  int err;

  if (!c->active)
    return 0;

  err= swapcontext(&c->base_context, &c->spawned_context);
  if (err)
  {
    fprintf(stderr, "Aieie, swapcontext() failed: %d (errno=%d)\n",
            err, errno);
    return -1;
  }

  return c->active;
}


int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d,
                 void *stack, size_t stack_size)
{
  int err;
  union pass_void_ptr_as_2_int u;

  if (2*sizeof(int) < sizeof(void *))
  {
    fprintf(stderr,
            "Error: Unable to store pointer in 2 ints on this architecture\n");
    return -1;
  }

  err= getcontext(&c->spawned_context);
  if (err)
    return -1;
  c->spawned_context.uc_stack.ss_sp= stack;
  c->spawned_context.uc_stack.ss_size= stack_size;
  c->spawned_context.uc_link= NULL;
  c->user_func= f;
  c->user_data= d;
  c->active= 1;
  u.p= c;
  makecontext(&c->spawned_context, my_context_spawn_internal, 2,
              u.a[0], u.a[1]);

  return my_context_continue(c);
}


int
my_context_yield(struct my_context *c)
{
  int err;

  if (!c->active)
    return -1;

  err= swapcontext(&c->spawned_context, &c->base_context);
  if (err)
    return -1;
  return 0;
}
