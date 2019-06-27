#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <mips/trapframe.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

pid_t
sys_fork(struct trapframe *parent_tf, pid_t *retval) {
  KASSERT(curproc != NULL);

  struct proc *child_proc;
  int result;

  //create child process structure
  child_proc = proc_create_runprogram(curproc->p_name);
  if(child_proc == NULL) {
    return ENOMEM;
  }

  //create new address space for child process
  struct addrspace *child_addrspace;
  struct addrspace *parent_addrspace;

  parent_addrspace = curproc_getas();
  result = as_copy(parent_addrspace, &child_addrspace);
  if(result) {
    proc_destroy(child_proc);
    return ENOMEM;
  }

  //set child process address space
  spinlock_acquire(&child_proc->p_lock);
  child_proc->p_addrspace = child_addrspace;
  spinlock_release(&child_proc->p_lock);

  //copy parent trapframe on the heap
  struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
  if(child_tf == NULL) {
    proc_destroy(child_proc);
    return ENOMEM;
  }
  memcpy(child_tf,parent_tf,sizeof(struct trapframe));

  //set parent child relationship
  spinlock_acquire(&child_proc->p_lock);
  child_proc->p_parent = curproc;
  spinlock_release(&child_proc->p_lock);

  spinlock_acquire(&curproc->p_lock);
  array_add(curproc->p_children,child_proc,NULL);
  spinlock_release(&curproc->p_lock);

  //create thread for chid process
  result = thread_fork(curthread->t_name,child_proc,enter_forked_process,(void *)child_tf,0);
  if(result) {
    proc_destroy(child_proc);
    kfree(child_tf);
    return ENOMEM;
  }

  //set return value for parent
  *retval = child_proc->pid;

  //free copied trapframe on heap
 // kfree(child_tf);

  //return 0 to child process if fork success
  return (0);
}
