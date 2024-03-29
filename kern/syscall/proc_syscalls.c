#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <vfs.h>
#include <vm.h>
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

   #if OPT_A2

  struct proc *parent = p->p_parent;

  if(parent == NULL) { /*no living parent, can fully delete*/
    /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
    proc_destroy(p);
  } else { /*has living parent. move to dead children array*/
    //set exited to true and save the exit_code
    lock_acquire(p->set_lock);
    p->exit_code = exitcode;
    p->exited = 1;
    lock_release(p->set_lock);

    //wake up waiting parent
    lock_acquire(p->wait_lock);
    cv_broadcast(p->wait_cv, p->wait_lock);
    lock_release(p->wait_lock);

    DEBUG(DB_SYSCALL, "moving living to dead \n");
    lock_acquire(parent->set_lock);
    for(unsigned i = 0; i < array_num(parent->p_living_children); i++) {
      struct proc *child = array_get(parent->p_living_children,i);
      if(child->pid == p->pid) {
        array_add(parent->p_dead_children,p,NULL);
        array_remove(parent->p_living_children,i);
        break;
      }
    }
    lock_release(parent->set_lock);

  }

  #endif
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
  *retval = curproc->pid;;
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
  KASSERT(status!= NULL);

  if (options != 0) {
    return(EINVAL);
  }
  
  #if OPT_A2
  //search through living_children array
  struct proc* living_child = look_through_children(curproc->p_living_children,pid);
  //search through dead_children array
  struct proc* dead_child = look_through_children(curproc->p_dead_children, pid);

  //if the child is dead fetch exit code
  if(dead_child != NULL){
    exitstatus = _MKWAIT_EXIT(dead_child->exit_code);
  }else if (living_child != NULL) { //if child is living, go to block
    lock_acquire(living_child->wait_lock);
    while(living_child->exited == 0) {
      cv_wait(living_child->wait_cv, living_child->wait_lock);
    }
    lock_release(living_child->wait_lock);
    exitstatus = _MKWAIT_EXIT(living_child->exit_code);
  }else{ //if not one of curproc children return -1
    *retval = -1;
    return(EINVAL);
  }

  #else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  #endif

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
  array_add(curproc->p_living_children,child_proc,NULL);
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

#if OPT_A2

int sys_execv(const char *program, char **args) {
	int result;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	(void)args;

	//counting args
  	int argc = 0;
  	while(args[argc] != NULL) {
    		argc++;
 	 }
  //	kprintf("number of args: %d \n", argc);

	  //allocate space for string params
  char **argv = (char **)kmalloc((argc+1) * sizeof(char *));
  for(int i = 0; i < argc; i++) {
    argv[i] = (char *)kmalloc(128 * sizeof(char));
    size_t arg_len = strlen(args[i]) + 1;
    if(argv[i] == NULL) {
      for(int j = 0; j < i; j++) {
        kfree(argv[j]);
      }
      kfree(argv);
      return ENOMEM;
    }
    //copy individual args to kernel
    result = copyin((const_userptr_t)args[i], (void *)argv[i],arg_len);
    if(result) {
      for(int j = 0; j < i; j++) {
        kfree(argv[j]);
      }
      kfree(argv);
      return result;
    }
   // kprintf("%s \n",argv[i]);
  }
  argv[argc] = NULL;

  //copy path from user space to kernel
	size_t program_len = (strlen(program) + 1) * sizeof(char);
	char *kern_program = kmalloc(program_len); //allocate space in kernel space
	if(kern_program == NULL) {
		return ENOMEM;
	}
	result = copyin((const_userptr_t)program,(void *)kern_program,program_len);
	if(result) {
		kfree(kern_program);
		return result;
	}

	//open program file
	char *kprog_temp = kstrdup(kern_program);
	result = vfs_open(kprog_temp, O_RDONLY, 0, &v);
	if(result) {
		kfree(kprog_temp);
		kfree(kern_program);
		return result;
	}

	//create new address space
	as = as_create();
	if (as == NULL) {
		kfree(kprog_temp);
		kfree(kern_program);
		vfs_close(v);
		return ENOMEM;
	}

	//set process to new address space and activate it
	struct addrspace *old_as = curproc_setas(as);
	//as_activate();

	//load program
	result = load_elf(v, &entrypoint);
	if (result) {
		kfree(kprog_temp);
		kfree(kern_program);
		as_deactivate();
		as = curproc_setas(old_as);
		as_destroy(as);
		vfs_close(v);
		return result;
	}
	vfs_close(v);
	kfree(kprog_temp);
	kfree(kern_program);

	result = as_define_stack_arg(as, &stackptr, argc, argv);
/*
	//copy onto user stack
	result = as_define_stack(as, &stackptr);
	if (result) {
		as_deactivate();
		as = curproc_setas(old_as);
		as_destroy(as);
		return result;
	}

  vaddr_t temp_stackptr = stackptr; //tracker pointer
  vaddr_t *arg_ptr = kmalloc((argc + 1) * sizeof(vaddr_t)); //array of pointer to argument srtrings on stack
  size_t total_string_size  = 0;

  kprintf("putting strings on stack\n");
  //put strings on to stack
  for(int i = argc-1; i >= 0; i--) {
    size_t arg_len = strlen(argv[i]) + 1;
    size_t arg_size = arg_len * sizeof(char);
    total_string_size += arg_size;
    temp_stackptr -= arg_size; //address of start of string

    kprintf("copying: %s\n",argv[i]);
  
    result = copyout((void *)argv[i], (userptr_t) temp_stackptr, arg_len);
    if(result) {
      for(int i = 0; i < argc; i++) {
        kfree(argv[i]);
      }
      kfree(argv);
      kfree(arg_ptr);
      as = curproc_setas(old_as);
      as_destroy(as);
      return result;
    }
    arg_ptr[i] = temp_stackptr;
    kprintf("copied: %s\n",(char *)arg_ptr[i]);
  }
  arg_ptr[argc] = (vaddr_t) NULL;

  //alignment for ptrs
  temp_stackptr = stackptr-ROUNDUP(total_string_size,4);
  KASSERT(temp_stackptr % 4 == 0);

  kprintf("putting ptrs on stack\n");
  //put string ptrs on to stack
  //calculate lowest address needed
  size_t ptr_size = sizeof(vaddr_t);
  size_t total_array_size = ptr_size * (argc + 1);
  temp_stackptr -= total_array_size;
  KASSERT(temp_stackptr % 4 == 0);
  result = copyout((void *)arg_ptr, (userptr_t) temp_stackptr, total_array_size);
  *//*
  for(int i = argc ; i >= 0; i--) {
    size_t ptr_size = sizeof(vaddr_t);
    temp_stackptr -= ptr_size;
    result = copyout((void *)&arg_ptr[i], (userptr_t) temp_stackptr, ptr_size);
    if(result) {
      for(int i = 0; i < argc; i++) {
        kfree(argv[i]);
      }
      kfree(argv);
      kfree(arg_ptr);
      as = curproc_setas(old_as);
      as_destroy(as);
      return result;
    }
  }*/
  if(result) {
    for(int i = 0; i < argc; i++) {
      kfree(argv[i]);
    }
    kfree(argv);
    //kfree(arg_ptr);
    as = curproc_setas(old_as);
    as_destroy(as);
    return result;
  }


	//delete old address space
  for(int i = 0; i < argc; i++) {
      kfree(argv[i]);
  }
  kfree(argv);
  as_destroy(old_as);

  //kprintf("entering new process\n");
	//enter new process
	enter_new_process(argc,(userptr_t)stackptr,ROUNDUP(stackptr, 8),entrypoint);


	panic("enter_new_process returned\n");
	return EINVAL;

}

#endif
