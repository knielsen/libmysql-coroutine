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
  Simple API for spawning a co-routine, to be used for async libmysqlclient.

  Idea is that by implementing this interface using whatever facilities are
  available for given platform, we can use the same code for the generic
  libmysqlclient-async code.

  (This particular implementation uses Posix ucontext swapcontext().)
*/

#include <ucontext.h>

struct my_context {
  void (*user_func)(void *);
  void *user_data;
  ucontext_t base_context;
  ucontext_t spawned_context;
  int active;
};


/*
  Spawn an asynchroneous context. The context will run the supplied user
  function, passing the supplied user data pointer.

  The user function may call my_context_yield(), which will cause this
  function to return 1. Then later my_context_continue() may be called, which
  will resume the asynchroneous context by returning from the previous
  my_context_yield() call.

  When the user function returns, this function returns 0.

  In case of error, -1 is returned.
*/
extern int my_context_spawn(struct my_context *c, void (*f)(void *), void *d,
                            void *stack, size_t stack_size);

/*
  Suspend an asynchroneous context started with my_context_spawn.

  When my_context_yield() is called, execution immediately returns from the
  last my_context_spawn() or my_context_continue() call. Then when later
  my_context_continue() is called, execution resumes by returning from this
  my_context_yield() call.

  Returns 0 if ok, -1 in case of error.
*/
extern int my_context_yield(struct my_context *c);

/*
  Resume an asynchroneous context. The context was spawned by
  my_context_spawn(), and later suspended inside my_context_yield().

  The asynchroneous context may be repeatedly suspended with
  my_context_yield() and resumed with my_context_continue().

  Each time it is suspended, this function returns 1. When the originally
  spawned user function returns, this function returns 0.

  In case of error, -1 is returned.
*/
extern int my_context_continue(struct my_context *c);
