#include "kernel_sched.h"
#include "kernel_cc.h"
/**
  @file kernel_cc.c

  @brief The implementation for concurrency control .
  */
/*
 *  Pre-emption control
 */
int set_core_preemption(int preempt) {
    sig_atomic_t old_preempt;
    if (preempt) {
        old_preempt = __atomic_exchange_n(&CURCORE.preemption, preempt, __ATOMIC_RELAXED);
        cpu_enable_interrupts();
    } else {
        cpu_disable_interrupts();
        old_preempt = __atomic_exchange_n(&CURCORE.preemption, preempt, __ATOMIC_RELAXED);
    }
    return old_preempt;
}
int get_core_preemption() {
    return CURCORE.preemption;
}
/*  Locks for scheduler and device drivers. Because we support
 *  multiple cores, we need to avoid race conditions
 *  with an interrupt handler on the same core, and also to
 *  avoid race conditions between cores.
 *
*/
/*
 *
 * The kernel locks
 *
 */
Mutex kernel_mutex = MUTEX_INIT;          /* lock for resource tables */
/*Our edits*/
void Mutex_Lock(Mutex *lock) {
#define MUTEX_SPINS 1000
#define MAX_SPIN_COUNTER (10)
    int spin = MUTEX_SPINS;
    int spin_counter = 0;//Used to detect need for priority inversion
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            __builtin_ia32_pause();
            if (spin > 0) { spin--; }
            else {
                spin = MUTEX_SPINS;
                spin_counter++;
                if (get_core_preemption()) {
                    if (spin_counter >= MAX_SPIN_COUNTER) {
                        ASSERT(CURTHREAD != NULL);
                        CURTHREAD->yield_state = DEADLOCKED;
                    }
                    yield();
                }
            }
        }
    }
#undef MUTEX_SPINS
}
/*
  Pre-emption aware mutex.
  -------------------------

  This mutex will act as a mx if preemption is off, and a
  yielding mutex if it is on.

 */
void Mutex_Unlock(Mutex *lock) {
    __atomic_clear(lock, __ATOMIC_RELEASE);
}
/** \cond HELPER Helper structure for condition variables. */
typedef struct __cv_waitset_node {
    void *thread;
    struct __cv_waitset_node *next;
} __cv_waitset_node;
/** \endcond */
/*
  Condition variables.
*/
int Cond_Wait(Mutex *mutex, CondVar *cv) {
    __cv_waitset_node newnode;
    newnode.thread = CURTHREAD;
    Mutex_Lock(&(cv->waitset_lock));
    /* We just push the current thread to the head of the list */
    newnode.next = cv->waitset;
    cv->waitset = &newnode;
    /* Now atomically release mutex and sleep */
    Mutex_Unlock(mutex);
    sleep_releasing(STOPPED, &(cv->waitset_lock));
    /* Re-lock mutex before returning */
    Mutex_Lock(mutex);
    return 1;
}
/* Used to detect if a threads waits for IO for increasing its priority*/
int Cond_Wait_from_IO(Mutex *mutex, CondVar *cv) {
    ASSERT(CURTHREAD != NULL);
    CURTHREAD->yield_state = IO;
    return Cond_Wait(mutex, cv);
}
int Cond_Wait_with_timeout(Mutex *mutex, CondVar *cv, timeout_t timeout) {
    TimeoutCB *timeoutCB = (TimeoutCB *) xmalloc(sizeof(TimeoutCB));
    timeoutCB->tid = (Tid_t) CURTHREAD;
    timeoutCB->birthday = jiff;
    timeoutCB->timeout = timeout * 1000;
    timeoutCB->cv = cv;
    rlnode timeoutNode;
    rlnode_init(&timeoutNode, timeoutCB);
    rlist_push_back(&timeoutList, &timeoutNode);
    int retVal = Cond_Wait(mutex, cv);
    rlist_remove(&timeoutNode);
    return retVal;
}
/**
  @internal
  Helper for Cond_Signal and Cond_Broadcast
 */
static __cv_waitset_node *cv_signal(CondVar *cv) {
    /* Wakeup first process in the waiters' queue, if it exists. */
    if (cv->waitset != NULL) {
        __cv_waitset_node *node = cv->waitset;
        cv->waitset = node->next;
        wakeup(node->thread);
    }
    return cv->waitset;
}
void Cond_Signal(CondVar *cv) {
    Mutex_Lock(&(cv->waitset_lock));
    cv_signal(cv);
    Mutex_Unlock(&(cv->waitset_lock));
}
void Cond_Broadcast(CondVar *cv) {
    Mutex_Lock(&(cv->waitset_lock));
    while (cv_signal(cv))  /*loop*/;
    Mutex_Unlock(&(cv->waitset_lock));
}