/*
 * softirq.c - handles backend processing (I/O, timers, ingress packets, etc.)
 */

#include <base/stddef.h>
#include <base/log.h>
#include <runtime/thread.h>

#include "defs.h"
#include "net/defs.h"

static bool softirq_iokernel_pending(struct kthread *k)
{
	return !lrpc_empty(&k->rxq);
}

bool softirq_directpath_pending(struct kthread *k)
{
	return rx_pending(k->directpath_rxq);
}

static bool softirq_timer_pending(struct kthread *k, uint64_t now_tsc)
{
	uint64_t now_us = (now_tsc - start_tsc) / cycles_per_us;

	return ACCESS_ONCE(k->timern) > 0 &&
	       ACCESS_ONCE(k->timers[0].deadline_us) <= now_us;
}

static bool softirq_storage_pending(struct kthread *k)
{
	return storage_available_completions(&k->storage_q);
}

/**
 * softirq_pending - is there a softirq pending?
 */
bool softirq_pending(struct kthread *k, uint64_t now_tsc)
{
	return softirq_iokernel_pending(k) || softirq_directpath_pending(k) ||
	       softirq_timer_pending(k, now_tsc) || softirq_storage_pending(k);
}

/**
 * softirq_run_locked - schedule softirq work with kthread lock held
 * @k: the kthread to check for softirq work
 *
 * The kthread's lock must be held when calling this function.
 *
 * Returns true if softirq work was scheduled.
 */
bool softirq_run_locked(struct kthread *k)
{
	uint64_t now_tsc = rdtsc();
	bool work_done = false;

	assert_preempt_disabled();
	assert_spin_lock_held(&k->lock);

	/* check for iokernel softirq work */
	if (!k->iokernel_sched && softirq_iokernel_pending(k)) {
		k->iokernel_sched = true;
		thread_ready_head_locked(k->iokernel_softirq, -1);
		work_done = true;
	}

	/* check for directpath softirq work */
	if (!k->directpath_sched && softirq_directpath_pending(k)) {
		k->directpath_sched = true;
		thread_ready_head_locked(k->directpath_softirq, -1);
		work_done = true;
	}

	/* check for timer softirq work */
	if (!k->timer_sched && softirq_timer_pending(k, now_tsc)) {
		k->timer_sched = true;
		thread_ready_head_locked(k->timer_softirq, -1);
		work_done = true;
	}

	/* check for storage softirq work */
	if (!k->storage_sched && softirq_storage_pending(k)) {
		k->storage_sched = true;
		thread_ready_head_locked(k->storage_softirq, -1);
		work_done = true;
	}

	k->last_softirq_tsc = now_tsc;
	return work_done;
}

/**
 * softirq_run - schedule softirq work
 * Returns true if softirq work was scheduled.
 */
bool softirq_run(void)
{
	bool work_done;

	preempt_disable();
	work_done = softirq_run_preempt_disabled();
	preempt_enable();

	return work_done;
}

bool softirq_run_preempt_disabled(void)
{
	struct kthread *k;
	uint64_t now_tsc = rdtsc();
	bool work_done;

	assert_preempt_disabled();

	k = myk();
	if (!softirq_pending(k, now_tsc)) {
		k->last_softirq_tsc = now_tsc;
		return false;
	}
	spin_lock(&k->lock);
	work_done = softirq_run_locked(k);
	spin_unlock(&k->lock);

	return work_done;
}
