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

#include <ucontext.h>

struct my_state {
  int ret_status;
};

static int
do_out(int x)
{
  printf("%d\n", x);
    return x % 3;
}

static int
foo(struct my_state *s, int param)
{
  if (param <= 0)
    return -1;                                  /* Error */

  while (param)
  {
    int res= do_out(param--);
    printf("Result=%d\n", res);
  }

  return 0;
}

static int
foo_start(int *ret, struct my_state *s, int param)
{
  *ret= foo(s, param);
  return 0;
}

static int
foo_cont(int *ret, struct my_state *s)
{
  fprintf(stderr,
          "Error: calling foo_cont() without foo_start() being active\n");
  *ret= -1;
  return 0;
}

static void
state_init(struct my_state *s)
{
}

int
main(int argc, char *argv[])
{
  struct my_state s;
  int stat;
  int ret;

  state_init(&s);
  printf("Sync:\n");
  ret= foo(&s, 4);
  printf("Sync done: %d\n", ret);

  printf("\nAsync:\n");
  stat= foo_start(&ret, &s, 4);
  while (stat)
  {
    printf("...again, stata=%d\n", stat);
    stat= foo_cont(&ret, &s);
  }
  printf("Async done: %d\n", ret);

  return 0;
}
