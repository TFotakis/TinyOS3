
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/**
  @brief Create a new thread in the current process.
  */
Tid_t CreateThread(Task task, int argl, void* args)
{
  /*TCB* taskTcb = spawn_multithread(CURPROC, task, int argl, void* args);
  T_Info tmp = {
    .exitval = 0,
    .thread,
    .task = task,
    .argl = argl,
    .args = args,
    .condVar = NULL;
  }
  rlnode node;
  rlnode_new(&node, tmp);
  rlist_push_back(&CURPROC->t_info_list, node);*/
  return NOTHREAD/*(Tid_t) taskTcb*/;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t ThreadSelf()
{
  return (Tid_t) CURTHREAD;
}

/**
  @brief Join the given thread.
  */
int ThreadJoin(Tid_t tid, int* exitval)
{
  return -1;
}

/**
  @brief Detach the given thread.
  */
int ThreadDetach(Tid_t tid)
{
  return -1;
}

/**
  @brief Terminate the current thread.
  */
void ThreadExit(int exitval)
{
}


/**
  @brief Awaken the thread, if it is sleeping.

  This call will set the interrupt flag of the
  thread.

  */
int ThreadInterrupt(Tid_t tid)
{
  return -1;
}


/**
  @brief Return the interrupt flag of the
  current thread.
  */
int ThreadIsInterrupted()
{
  return 0;
}

/**
  @brief Clear the interrupt flag of the
  current thread.
  */
void ThreadClearInterrupt()
{
}