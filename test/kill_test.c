#include "kthread.h"
#include "proc.h"
#include "sched.h"

const int NUM_INITS = 6;

typedef void (*init_func_t)();
init_func_t init_funcs[] = {
    (init_func_t)mem_init, slab_init,  proc_init,
    kthread_init,          sched_init, proc_idleproc_init};

static context_t bootstrap_ctx;

#define KILL_STATUS 99

static int victim_ran = 0;
static int witness_ran = 0;

static void *victimproc_run(long arg1, void *arg2) {
    victim_ran = 1; // must never happen because it was killed
    return NULL;
}

static void *witnessproc_run(long arg1, void *arg2) {
    witness_ran = 1;
    return NULL;
}

static void *initproc_run(long arg1, void *arg2) {
    proc_t *victim = proc_create("victim");
    kthread_t *victim_thr = kthread_create(victim, victimproc_run, 0, NULL);
    proc_t *witness = proc_create("witness");
    kthread_t *witness_thr = kthread_create(witness, witnessproc_run, 0, NULL);

    proc_kill(victim, KILL_STATUS);

    // KILLED STATE
    if (victim->p_status != KILL_STATUS)
        return (void *)-1;
    if (victim_thr->kt_cancelled != 1)
        return (void *)-1;
    if (victim_thr->kt_retval != (void *)KILL_STATUS)
        return (void *)-1;
    if (witness_thr->kt_cancelled != 0)
        return (void *)-1;

    // Add both to the queue and run them
    curthr->kt_state = KT_ON_CPU;
    list_insert_back(&kt_runq.tq_list, &victim_thr->kt_qlink);
    list_insert_back(&kt_runq.tq_list, &witness_thr->kt_qlink);
    sched_switch();

    // AFTER STATE
    if (witness_ran != 1)
        return (void *)-1;
    if (witness->p_state != PROC_DEAD)
        return (void *)-1;
    if (victim_ran != 0)
        return (void *)-1;
    if (victim->p_status != KILL_STATUS)
        return (void *)-1;
    if (curproc != proc_initproc)
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

    return 0;
}
