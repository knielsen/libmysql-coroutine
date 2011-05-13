/*
  GCC-amd64 implementation of my_context.

  This is slightly optimised in the common case where we never yield
  (eg. fetch next row and it is already fully received in buffer). In this
  case we do not need to restore registers at return (though we still need to
  save them as we cannot know if we will yield or not in advance).

  Incomplete pseudocode only at this point.
*/

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d,
                 void *stack, size_t stack_size)
{
  int ret;
  c->alt_stack= stack;
  c->alt_stack_size= stack_size;
  c->stack_2= stack + stack_size;
  __asm(
        //save regs in c->regs_1
        mov %rsp, c->stack_1
        mov %lab_for_yield, c->ip_yield
        mov %lab_for_done, c->ip_done
        mov c->stack_2, %rsp
        //Add constraint to force parameter d in correct ABI register
        call (*<f>)
        jump *(c->ip_for_done)
      lab_for_done:
        // We do not need to restore caller-save registers if we never yielded
        mov $0, %ret  // Eg. register constraint for local var ret
        jump lab
      lab_for_yield:
        mov $1, %ret
      lab:
  );
  return ret;
}

int
my_context_continue(struct my_context *c)
{
  int ret;
  __asm(
        //save regs in c->regs_1
        mov %rsp, c->stack_1
        mov %lab_for_yield, c->ip_yield
        mov %lab_for_done, c->ip_done
        // restore regs from c->regs_2
        mov c->stack_2, %rsp
        jump *(c->ip_cont)
      lab_for_done:
        // restore regs from c->regs_1
        mov c->stack_1, %rsp
        mov $0, %ret  // Eg. register constraint for local var ret
        jump lab
      lab_for_yield:
        mov $1, %ret
      lab:
        );
  return ret;
}

int
my_context_yield(struct my_context *c)
{
  __asm(
        //save regs in c->regs_2
        mov %rsp, c->stack_2
        mov lab_for_cont, c->ip_cont
        //restore regs from c->regs_1
        mov c->stack_1, %rsp
        jump *(c->ip_yield)
      lab_for_cont:
        );
  return 0;
}
