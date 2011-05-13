/*
  GCC-amd64 implementation of my_context.

  This is slightly optimized in the common case where we never yield
  (eg. fetch next row and it is already fully received in buffer). In this
  case we do not need to restore registers at return (though we still need to
  save them as we cannot know if we will yield or not in advance).
*/

#include <stdint.h>
#include <stdlib.h>

#include "my_context.h"

/*
  Layout of saved registers etc.
  Since this is accessed through gcc inline assembler, it is easier to just
  use numbers than trying to define nice constants or structs.

   0    0   %rsp for suspended context
   1    8   %rbp for suspended context
   2   16   %rbx for suspended context
   3   24   %r12 for suspended context
   4   32   %r13 for suspended context
   5   40   %r14 for suspended context
   6   48   %r15 for suspended context
   7   56   %rip for continue
   8   64   %rsp for application context
   9   72   %rbp for application context
  10   80   %rbx for application context
  11   88   %r12 for application context
  12   96   %r13 for application context
  13  104   %r14 for application context
  14  112   %r15 for application context
  15  120   %rip for done
  16  128   %rip for yield
*/

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d,
                 void *stack, size_t stack_size)
{
  int ret;
  void *stack_2= stack + stack_size;

  /*
    There are 6 callee-save registers we need to save and restore when
    suspending and continuing, plus stack pointer %rsp and instruction pointer
    %rip.

    However, if we never suspend, the user-supplied function will in any case
    restore the 6 callee-save registers, so we can avoid restoring them in
    this case.
  */
  __asm__ __volatile__
    (
     "movq %%rsp, 64(%[save])\n\t"
     "movq %[stack_2], %%rsp\n\t"
     "movq %%rbp, 72(%[save])\n\t"
     "movq %%rbx, 80(%[save])\n\t"
     "movq %%r12, 88(%[save])\n\t"
     "movq %%r13, 96(%[save])\n\t"
     "movq %%r14, 104(%[save])\n\t"
     "movq %%r15, 112(%[save])\n\t"
     "leaq 1f(%%rip), %%rax\n\t"
     "leaq 2f(%%rip), %%rcx\n\t"
     "movq %%rax, 120(%[save])\n\t"
     "movq %%rcx, 128(%[save])\n\t"
     /*
       Constraint below puts the argument to the user function into %rdi, as
       needed for the calling convention.
     */
     "callq *%[f]\n\t"
     "jmpq *120(%[save])\n"
     /*
       Come here when operation is done.
       We do not need to restore callee-save registers, as the called function
       will do this for us if needed.
     */
     "1:\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 3f\n"
     /* Come here when operation was suspended. */
     "2:\n\t"
     "movl $1, %[ret]\n"
     "3:\n"
     : [ret] "=a" (ret),
       [f] "+S" (f),
       /* Need this in %rdi to follow calling convention. */
       [d] "+D" (d)
     : [stack_2] "a" (stack_2),
       /* Need this in callee-save register to preserve in function call. */
       [save] "b" (&c->save[0])
     : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc"
  );
  return ret;
}

int
my_context_continue(struct my_context *c)
{
  int ret;
  __asm__ __volatile__
    (
     "movq %%rsp, 64(%[save])\n\t"
     "movq %%rbp, 72(%[save])\n\t"
     "movq %%rbx, 80(%[save])\n\t"
     "movq %%r12, 88(%[save])\n\t"
     "movq %%r13, 96(%[save])\n\t"
     "movq %%r14, 104(%[save])\n\t"
     "movq %%r15, 112(%[save])\n\t"
     "leaq 1f(%%rip), %%rax\n\t"
     "leaq 2f(%%rip), %%rcx\n\t"
     "movq %%rax, 120(%[save])\n\t"
     "movq %%rcx, 128(%[save])\n\t"

     "movq (%[save]), %%rsp\n\t"
     "movq 8(%[save]), %%rbp\n\t"
     "movq 16(%[save]), %%rbx\n\t"
     "movq 24(%[save]), %%r12\n\t"
     "movq 32(%[save]), %%r13\n\t"
     "movq 40(%[save]), %%r14\n\t"
     "movq 48(%[save]), %%r15\n\t"
     "jmpq *56(%[save])\n"
     /*
       Come here when operation is done.
       Be sure to use the same callee-save register for %[save] here and in
       my_context_spawn(), so we preserve the value correctly at this point.
     */
     "1:\n\t"
     "movq 64(%[save]), %%rsp\n\t"
     "movq 72(%[save]), %%rbp\n\t"
     "movq 80(%[save]), %%rbx\n\t"
     "movq 88(%[save]), %%r12\n\t"
     "movq 96(%[save]), %%r13\n\t"
     "movq 104(%[save]), %%r14\n\t"
     "movq 112(%[save]), %%r15\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 3f\n"
     /* Come here when operation is suspended. */
     "2:\n\t"
     "movl $1, %[ret]\n"
     "3:\n"
     : [ret] "=a" (ret)
     : /* Need this in callee-save register to preserve in function call. */
       [save] "b" (&c->save[0])
     : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory", "cc"
        );
  return ret;
}

int
my_context_yield(struct my_context *c)
{
  uint64_t *save= &c->save[0];
  __asm__ __volatile__
    (
     "movq %%rsp, (%[save])\n\t"
     "movq %%rbp, 8(%[save])\n\t"
     "movq %%rbx, 16(%[save])\n\t"
     "movq %%r12, 24(%[save])\n\t"
     "movq %%r13, 32(%[save])\n\t"
     "movq %%r14, 40(%[save])\n\t"
     "movq %%r15, 48(%[save])\n\t"
     "leaq 1f(%%rip), %%rax\n\t"
     "movq %%rax, 56(%[save])\n\t"

     "movq 64(%[save]), %%rsp\n\t"
     "movq 72(%[save]), %%rbp\n\t"
     "movq 80(%[save]), %%rbx\n\t"
     "movq 88(%[save]), %%r12\n\t"
     "movq 96(%[save]), %%r13\n\t"
     "movq 104(%[save]), %%r14\n\t"
     "movq 112(%[save]), %%r15\n\t"
     "jmpq *128(%[save])\n"

     "1:\n"
     : [save] "+D" (save)
     :
     : "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory", "cc"
     );
  return 0;
}
