#include "kthread.h"
#include "proc.h"
#include "sched.h"

const int NUM_INITS = 6;

typedef void (*init_func_t)();
init_func_t init_funcs[] = {
    (init_func_t)mem_init, slab_init,  proc_init,
    kthread_init,          sched_init, proc_idleproc_init};

static context_t bootstrap_ctx;

static proc_t *child;
static kthread_t *child_thr;
static int child_ran = 0;

static void *childproc_run(long arg1, void *arg2) {
    // CHILD STATE
    if (curproc != child)
        return (void *)-1;
    if (curthr != child_thr)
        return (void *)-1;
    if (curproc->p_pproc != proc_initproc)
        return (void *)-1;

    child_ran = 1;
    return NULL;
}

static void *initproc_run(long arg1, void *arg2) {
    // Fork by creating a process parented to init that will run childproc_run
    child = proc_create("child");
    child_thr = kthread_create(child, childproc_run, 0, NULL);

    // CREATION STATE
    if (child == NULL || child_thr == NULL)
        return (void *)-1;
    if (child->p_pid != 2)
        return (void *)-1;
    if (child->p_pproc != proc_initproc)
        return (void *)-1;
    if (child->p_threads.size != 1)
        return (void *)-1;
    if (child_thr->kt_state != KT_RUNNABLE)
        return (void *)-1;
    if (curproc->p_children.size != 1)
        return (void *)-1;
    if (proc_list.size != 2)
        return (void *)-1; // init + child
    if (child_ran != 0)
        return (void *)-1; // child has not run yet

    // Run the child process and come back here
    curthr->kt_state =
        KT_ON_CPU; // So that scheduler puts it back into the queue
    list_insert_back(&kt_runq.tq_list, &child_thr->kt_qlink);
    sched_switch();

    // CHILD RAN STATE
    if (child_ran != 1)
        return (void *)-1;
    if (child->p_state != PROC_DEAD)
        return (void *)-1;
    if (child->p_status != 0)
        return (void *)-1;
    if (child_thr->kt_state != KT_EXITED)
        return (void *)-1;
    if (curproc != proc_initproc)
        return (void *)-1;
    if (kt_runq.tq_list.size != 0)
        return (void *)-1;

    return NULL;
}

void *start_initproc(long arg1, void *arg2) {
    proc_initproc = proc_create("init");
    kthread_t *init_thread =
        kthread_create(proc_initproc, initproc_run, 0, NULL);

    // don't worry about using the scheduling system...
    curproc = proc_initproc;
    curthr = init_thread;

    context_make_active(&init_thread->kt_ctx);

    return NULL;
}

int main(int argc, char **argv) {
    // initialize subsystems
    for (int i = 0; i < NUM_INITS; i++) {
        init_funcs[i]();
    }

    void *bootstrap_stack = page_alloc_n(1);
    if (bootstrap_stack == NULL) {
        return -1;
    }

    context_setup(&bootstrap_ctx, start_initproc, 0, NULL, bootstrap_stack,
                  PAGE_SIZE, NULL);
    context_switch(
        &bios_ctx,
        &bootstrap_ctx); // saves this as the place where bios ctx will restore

    // AFTER STATE
    if (proc_initproc->p_state != PROC_DEAD)
        return 1;
    if (proc_initproc->p_status != 0)
        return 1;
    if (child_ran != 1)
        return 1;
    if (child->p_state != PROC_DEAD)
        return 1;
    if (next_pid != 3)
        return 1; // idleproc 0, init 1, child 2
    if (curproc != proc_initproc)
        return 1;
    if (curthr->kt_state != KT_EXITED)
        return 1;

    return 0;
}
