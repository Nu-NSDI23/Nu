/*
 * sched.c - a scheduler for user-level threads
 */

#include <sched.h>

#include <base/hash.h>
#include <base/limits.h>
#include <base/list.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/slab.h>
#include <base/stddef.h>
#include <base/tcache.h>
#include <runtime/sync.h>
#include <runtime/thread.h>

#include "defs.h"

const int thread_link_offset = offsetof(struct thread, link);
const int thread_monitor_cnt_offset =
       offsetof(struct thread, nu_state) +
       offsetof(struct thread_nu_state, monitor_cnt);
const int thread_owner_proclet_offset =
       offsetof(struct thread, nu_state) +
       offsetof(struct thread_nu_state, owner_proclet);
const int thread_proclet_slab_offset =
       offsetof(struct thread, nu_state) +
       offsetof(struct thread_nu_state, proclet_slab);

/* the current running thread, or NULL if there isn't one */
__thread thread_t *__self;
/* a pointer to the top of the per-kthread (TLS) runtime stack */
static __thread void *runtime_stack;
/* a pointer to the bottom of the per-kthread (TLS) runtime stack */
static __thread void *runtime_stack_base;

/* Flag to prevent watchdog from running */
bool disable_watchdog;

/* real-time compute congestion signals (shared with the iokernel) */
struct congestion_info *runtime_congestion;

/* fast allocation of struct thread */
static struct slab thread_slab;
static struct tcache *thread_tcache;
static DEFINE_PERTHREAD(struct tcache_perthread, thread_pt);

extern struct tcache *stack_tcache;

/* used to track cycle usage in scheduler */
static __thread uint64_t last_tsc;

static void *pause_req_owner_proclet = NULL;
static bool global_pause_req_mask = false;
static void *global_prioritized_rcu = NULL;
LIST_HEAD(all_migrating_ths);

/**
 * In inc/runtime/thread.h, this function is declared inline (rather than static
 * inline) so that it is accessible to the Rust bindings. As a result, it must
 * also appear in a source file to avoid linker errors.
 */
thread_t *thread_self(void);

/**
 * cores_have_affinity - returns true if two cores have cache affinity
 * @cpua: the first core
 * @cpub: the second core
 */
static inline bool cores_have_affinity(unsigned int cpua, unsigned int cpub)
{
	return cpua == cpub ||
	       cpu_map[cpua].sibling_core == cpub;
}

/**
 * jmp_thread - runs a thread, popping its trap frame
 * @th: the thread to run
 *
 * This function restores the state of the thread and switches from the runtime
 * stack to the thread's stack. Runtime state is not saved.
 */
static __noreturn void jmp_thread(thread_t *th)
{
	assert_preempt_disabled();
	assert(th->thread_ready);

	__self = th;
	th->thread_ready = false;
	if (unlikely(load_acquire(&th->thread_running))) {
		/* wait until the scheduler finishes switching stacks */
		while (load_acquire(&th->thread_running))
			cpu_relax();
	}
	th->thread_running = true;
	__jmp_thread(&th->nu_state.tf);
}

/**
 * jmp_thread_direct - runs a thread, popping its trap frame
 * @oldth: the last thread to run
 * @newth: the next thread to run
 *
 * This function restores the state of the thread and switches from the runtime
 * stack to the thread's stack. Runtime state is not saved.
 */
static void jmp_thread_direct(thread_t *oldth, thread_t *newth)
{
	assert_preempt_disabled();
	assert(newth->thread_ready);

	__self = newth;
	newth->thread_ready = false;
	if (unlikely(load_acquire(&newth->thread_running))) {
		/* wait until the scheduler finishes switching stacks */
		while (load_acquire(&newth->thread_running))
			cpu_relax();
	}
	newth->thread_running = true;
	__jmp_thread_direct(&oldth->nu_state.tf, &newth->nu_state.tf,
			    &oldth->thread_running);
}

/**
 * jmp_runtime - saves the current trap frame and jumps to a function in the
 *               runtime
 * @fn: the runtime function to call
 *
 * WARNING: Only threads can call this function.
 *
 * This function saves state of the running thread and switches to the runtime
 * stack, making it safe to run the thread elsewhere.
 */
static void jmp_runtime(runtime_fn_t fn)
{
	assert_preempt_disabled();
	assert(thread_self() != NULL);

	__jmp_runtime(&thread_self()->nu_state.tf, fn, runtime_stack);
}

/**
 * jmp_runtime_nosave - jumps to a function in the runtime without saving the
 *			caller's state
 * @fn: the runtime function to call
 */
static __noreturn void jmp_runtime_nosave(runtime_fn_t fn)
{
	assert_preempt_disabled();

	__jmp_runtime_nosave(fn, runtime_stack);
}

static void drain_overflow(struct kthread *l)
{
	thread_t *th;

	assert_spin_lock_held(&l->lock);
	assert(myk() == l || l->parked);

	while (l->rq_head - l->rq_tail < RUNTIME_RQ_SIZE) {
		th = list_pop(&l->rq_overflow, thread_t, link);
		if (!th)
			break;
		l->rq[l->rq_head++ % RUNTIME_RQ_SIZE] = th;
	}
}

static bool work_available(struct kthread *k, uint64_t now_tsc)
{
#ifdef GC
	if (get_gc_gen() != ACCESS_ONCE(k->local_gc_gen) &&
	    ACCESS_ONCE(k->parked)) {
		return true;
	}
#endif

	return ACCESS_ONCE(k->rq_tail) != ACCESS_ONCE(k->rq_head) ||
	       softirq_pending(k, now_tsc) ||
	       !list_empty_volatile(&k->rq_deprioritized);
}

static void update_oldest_tsc(struct kthread *k)
{
	thread_t *th;

	assert_spin_lock_held(&k->lock);

	/* find the oldest thread in the runqueue */
	if (load_acquire(&k->rq_head) != k->rq_tail) {
		th = k->rq[k->rq_tail % RUNTIME_RQ_SIZE];
		ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
	}
}

static void __pause_migrating_threads_locked(struct kthread *k)
{
	thread_t *th;
	thread_t sentinel;
	uint32_t i, avail, num_paused = 0;

	assert_spin_lock_held(&k->lock);
	avail = load_acquire(&k->rq_head) - k->rq_tail;
	for (i = 0; i < avail; i++) {
		th = k->rq[k->rq_tail++ % RUNTIME_RQ_SIZE];
		if (th->nu_state.owner_proclet == pause_req_owner_proclet) {
			num_paused++;
			list_add_tail(&k->migrating_ths, &th->link);
		} else {
			k->rq[k->rq_head % RUNTIME_RQ_SIZE] = th;
	                ACCESS_ONCE(k->rq_head)++;
		}
        }

	if (unlikely(!list_empty(&k->rq_overflow))) {
	        list_add_tail(&k->rq_overflow, &sentinel.link);
		while ((th = list_pop(&k->rq_overflow, thread_t, link)) !=
		       &sentinel) {
			if (th->nu_state.owner_proclet == pause_req_owner_proclet) {
				num_paused++;
				list_add_tail(&k->migrating_ths, &th->link);
			} else {
				list_add_tail(&k->rq_overflow, &th->link);
			}
		}
	}

	k->q_ptrs->rq_tail += num_paused;

	list_for_each(&k->migrating_ths, th, link) {
		if (unlikely(th->thread_running)) {
			while (load_acquire(&th->thread_running))
				cpu_relax();
		}
	}
	store_release(&k->pause_req, false);
}

static inline bool has_pending_pause_req(struct kthread *k) {
	return load_acquire(&k->pause_req) & global_pause_req_mask;
}

static inline void pause_local_migrating_threads_locked(void)
{
	struct kthread *l = myk();

	assert_spin_lock_held(&l->lock);
	if (has_pending_pause_req(l))
		__pause_migrating_threads_locked(l);
}

void pause_local_migrating_threads(void)
{
	struct kthread *l = getk();

	if (has_pending_pause_req(l)) {
		spin_lock(&l->lock);
		pause_local_migrating_threads_locked();
		spin_unlock(&l->lock);
	}
	putk();
}

bool thread_is_rcu_held(thread_t *th, void *rcu);

static void __prioritize_rcu_readers_locked(struct kthread *k)
{
	thread_t *th;
	thread_t sentinel;
	uint32_t i, avail, num_paused = 0;

	assert_spin_lock_held(&k->lock);
	avail = load_acquire(&k->rq_head) - k->rq_tail;
	for (i = 0; i < avail; i++) {
		th = k->rq[k->rq_tail++ % RUNTIME_RQ_SIZE];
		if (!thread_is_rcu_held(th, global_prioritized_rcu)) {
			num_paused++;
			list_add_tail(&k->rq_deprioritized, &th->link);
		} else {
			k->rq[k->rq_head % RUNTIME_RQ_SIZE] = th;
	                ACCESS_ONCE(k->rq_head)++;
		}
	}

	if (unlikely(!list_empty(&k->rq_overflow))) {
	        list_add_tail(&k->rq_overflow, &sentinel.link);
		while ((th = list_pop(&k->rq_overflow, thread_t, link)) !=
		       &sentinel) {
			if (!thread_is_rcu_held(th, global_prioritized_rcu)) {
				num_paused++;
				list_add_tail(&k->rq_deprioritized, &th->link);
			} else
				list_add_tail(&k->rq_overflow, &th->link);
		}
	}

	k->q_ptrs->rq_tail += num_paused;
	store_release(&k->prioritize_req, false);
}

static inline bool has_pending_prioritize_req(struct kthread *k) {
	return load_acquire(&k->prioritize_req) &
	       (!!global_prioritized_rcu);
}

static inline void prioritize_local_rcu_readers_locked(void)
{
	struct kthread *l = myk();

	assert_spin_lock_held(&l->lock);
	if (has_pending_prioritize_req(l))
		__prioritize_rcu_readers_locked(l);
}

void prioritize_local_rcu_readers(void)
{
	struct kthread *l = getk();

	if (has_pending_prioritize_req(l)) {
		spin_lock(&l->lock);
		prioritize_local_rcu_readers_locked();
		spin_unlock(&l->lock);
	}
	putk();
}

static void pop_deprioritized_threads_locked(struct kthread *k)
{
	thread_t *th;
	assert_spin_lock_held(&k->lock);

	while (!list_empty(&k->rq_deprioritized)) {
		th = list_pop(&k->rq_deprioritized, thread_t, link);
		th->thread_ready = false;
		thread_ready_locked(th);
	}
}

static inline bool can_handle_pause_req(struct kthread *k) {
	thread_t *th = k->curr_th;

	return !th || th->nu_state.owner_proclet != pause_req_owner_proclet;
}

static bool handle_pending_pause_req(struct kthread *k)
{
	bool handled;
	if (!spin_try_lock(&k->lock))
		return false;

	if (unlikely(!has_pending_pause_req(k))) {
		spin_unlock(&k->lock);
		return true;
	}

	if (!can_handle_pause_req(k))
		handled = false;
	else {
		__pause_migrating_threads_locked(k);
		handled = true;
	}

	spin_unlock(&k->lock);
	return handled;
}

static bool handle_pending_prioritize_req(struct kthread *k)
{
	bool handled;
	if (!spin_try_lock(&k->lock))
		return false;

	if (unlikely(!has_pending_prioritize_req(k))) {
		spin_unlock(&k->lock);
		return true;
	}

	if (ACCESS_ONCE(k->parked)) {
	        __prioritize_rcu_readers_locked(k);
		handled = true;
	}
	else
		handled = false;

	spin_unlock(&k->lock);
	return handled;
}

static inline bool handle_preemptor(void)
{
	struct kthread *k = myk();
	thread_t *th = k->preemptor->th;

	assert_spin_lock_held(&k->lock);
	assert_preempt_disabled();
	if (unlikely(th && !th->thread_running)) {
		thread_ready_head_locked(th,
					 k->preemptor->ready_tsc);
		k->preemptor->th = NULL;
		preempt_disable();
		return true;
	}
	return false;
}

void shed_work(void)
{
	struct kthread *l = myk();
	struct kthread *r;
	int i, j, start_idx, num;
	thread_t *th;

	spin_lock_np(&l->lock);
	softirq_run_locked(l);

	start_idx = rand_crc32c((uintptr_t)l);
	for (i = 0;
	     i < maxks && (l->rq_head != l->rq_tail || !list_empty(&l->rq_overflow));
	     i++) {
		r = ks[(start_idx + i) % maxks];
		if (l == r || r->parked || !spin_try_lock(&r->lock))
			continue;
		if (unlikely(r->parked)) {
			spin_unlock(&r->lock);
			continue;
		}
		prioritize_local_rcu_readers_locked();
		pause_local_migrating_threads_locked();

		num = MIN(l->rq_head - l->rq_tail,
			  RUNTIME_RQ_SIZE - (r->rq_head - r->rq_tail));
		for (j = 0; j < num; j++)
			r->rq[r->rq_head++ % RUNTIME_RQ_SIZE] =
				l->rq[l->rq_tail++ % RUNTIME_RQ_SIZE];

		while (true) {
			th = list_pop(&l->rq_overflow, thread_t, link);
			if (!th)
				break;
			list_add_tail(&r->rq_overflow, &th->link);
			num++;
		}

		ACCESS_ONCE(l->q_ptrs->rq_tail) += num;
		update_oldest_tsc(l);
		ACCESS_ONCE(r->q_ptrs->rq_head) += num;
		update_oldest_tsc(r);

		spin_unlock(&r->lock);
	}

	spin_unlock_np(&l->lock);
}

static bool steal_work(struct kthread *l, struct kthread *r)
{
	thread_t *th;
	uint64_t now_tsc = rdtsc();
	uint32_t i, avail, rq_tail, overflow = 0;

	assert_spin_lock_held(&l->lock);
	assert(l->rq_head == 0 && l->rq_tail == 0);

	if (unlikely(ACCESS_ONCE(r->pause_req)) &&
	    !handle_pending_pause_req(r))
	        return false;
	if (unlikely(ACCESS_ONCE(r->prioritize_req)) &&
	    !handle_pending_prioritize_req(r))
	        return false;
	if (!work_available(r, now_tsc))
		return false;
	if (!spin_try_lock(&r->lock))
		return false;

#ifdef GC
	if (unlikely(get_gc_gen() != r->local_gc_gen)) {
		if (!ACCESS_ONCE(r->parked)) {
			spin_unlock(&r->lock);
			return false;
		}
		gc_kthread_report(r);
		drain_overflow(r);
	}
#endif
	/* try to steal directly from the runqueue */
	avail = load_acquire(&r->rq_head) - r->rq_tail;
	if (avail) {
		/* steal half the tasks */
		avail = div_up(avail, 2);
		rq_tail = r->rq_tail;
		for (i = 0; i < avail; i++)
			l->rq[i] = r->rq[rq_tail++ % RUNTIME_RQ_SIZE];
		store_release(&r->rq_tail, rq_tail);

		/*
		 * Drain the remote overflow queue, so newly readied tasks
		 * don't cut in front tasks in the oveflow queue
		 */
		while (true) {
			th = list_pop(&r->rq_overflow, thread_t, link);
			if (!th)
				break;
			list_add_tail(&l->rq_overflow, &th->link);
			overflow++;
		}

		ACCESS_ONCE(r->q_ptrs->rq_tail) += avail + overflow;
		update_oldest_tsc(r);
		spin_unlock(&r->lock);

		l->rq_head = avail;
		update_oldest_tsc(l);
		ACCESS_ONCE(l->q_ptrs->rq_head) += avail + overflow;
		STAT(THREADS_STOLEN) += avail + overflow;
		return true;
	}

	/* check for overflow tasks */
	th = list_pop(&r->rq_overflow, thread_t, link);
	if (th) {
		ACCESS_ONCE(r->q_ptrs->rq_tail)++;
		update_oldest_tsc(r);
		spin_unlock(&r->lock);
		l->rq[l->rq_head++ % RUNTIME_RQ_SIZE] = th;
		ACCESS_ONCE(l->q_ptrs->oldest_tsc) = th->ready_tsc;
		ACCESS_ONCE(l->q_ptrs->rq_head)++;
		STAT(THREADS_STOLEN)++;
		return true;
	}

	if (unlikely(!list_empty(&r->rq_deprioritized))) {
		pop_deprioritized_threads_locked(r);
		spin_unlock(&r->lock);
		return true;
	}

	/* check for softirqs */
	if (softirq_run_locked(r)) {
		STAT(SOFTIRQS_STOLEN)++;
		spin_unlock(&r->lock);
		return true;
	}

	spin_unlock(&r->lock);
	return false;
}

static __noinline bool do_watchdog(struct kthread *l)
{
	bool work;

	assert_spin_lock_held(&l->lock);

	work = softirq_run_locked(l);
	if (work)
		STAT(SOFTIRQS_LOCAL)++;
	return work;
}

/* the main scheduler routine, decides what to run next */
static __noreturn __noinline void schedule(void)
{
	struct kthread *r = NULL, *l = myk();
	uint64_t start_tsc;
	thread_t *th = NULL;
	unsigned int start_idx;
	unsigned int iters = 0;
	int i, sibling;

	assert_spin_lock_held(&l->lock);
	assert(l->parked == false);

	/* unmark busy for the stack of the last uthread */
	if (likely(__self != NULL)) {
		store_release(&__self->thread_running, false);
		__self = NULL;
		l->curr_th = NULL;
	}

	/* detect misuse of preempt disable */
	BUG_ON((preempt_cnt & ~PREEMPT_NOT_PENDING) != 1);

	/* update entry stat counters */
	STAT(RESCHEDULES)++;
	start_tsc = rdtsc();
	STAT(PROGRAM_CYCLES) += start_tsc - last_tsc;

	/* increment the RCU generation number (even is in scheduler) */
	store_release(&l->rcu_gen, l->rcu_gen + 1);
	ACCESS_ONCE(l->q_ptrs->rcu_gen) = l->rcu_gen;
	assert((l->rcu_gen & 0x1) == 0x0);

	/* check for pending preemption */
	if (unlikely(preempt_cede_needed(l))) {
		l->parked = true;
		spin_unlock(&l->lock);
		kthread_park(false);
		start_tsc = rdtsc();
		iters = 0;
		spin_lock(&l->lock);
		l->parked = false;
	}

	prioritize_local_rcu_readers_locked();
	pause_local_migrating_threads_locked();
	if (handle_preemptor()) {
		goto done;
	}

#ifdef GC
	if (unlikely(get_gc_gen() != l->local_gc_gen))
		gc_kthread_report(l);
#endif

	/* if it's been too long, run the softirq handler */
	if (!disable_watchdog &&
	    unlikely(start_tsc - l->last_softirq_tsc >=
	             cycles_per_us * RUNTIME_WATCHDOG_US)) {
		l->last_softirq_tsc = start_tsc;
		if (do_watchdog(l)) {
			goto done;
		}
	}

	/* move overflow tasks into the runqueue */
	if (unlikely(!list_empty(&l->rq_overflow)))
		drain_overflow(l);

	/* first try the local runqueue */
	if (l->rq_head != l->rq_tail) {
		goto done;
	}

	/* reset the local runqueue since it's empty */
	l->rq_head = l->rq_tail = 0;

again:
	prioritize_local_rcu_readers_locked();
	pause_local_migrating_threads_locked();
	if (handle_preemptor()) {
		goto done;
	}

	/* then check for local softirqs */
	if (softirq_run_locked(l)) {
		STAT(SOFTIRQS_LOCAL)++;
		goto done;
	}

	/* then try to steal from a sibling kthread */
	sibling = cpu_map[l->curr_cpu].sibling_core;
	r = cpu_map[sibling].recent_kthread;
	if (r && r != l && steal_work(l, r)) {
		goto done;
	}

	/* try to steal from every kthread */
	start_idx = rand_crc32c((uintptr_t)l);
	for (i = 0; i < maxks; i++) {
		int idx = (start_idx + i) % maxks;
		if (ks[idx] != l && steal_work(l, ks[idx])) {
			goto done;
		}
		if (r && r != l && steal_work(l, r)) {
			goto done;
		}
	}

	if (unlikely(!list_empty(&l->rq_deprioritized))) {
		pop_deprioritized_threads_locked(l);
		goto done;
	}

	/* recheck for local softirqs one last time */
	if (softirq_run_locked(l)) {
		STAT(SOFTIRQS_LOCAL)++;
		goto done;
	}

#ifdef GC
	if (unlikely(get_gc_gen() != l->local_gc_gen))
		gc_kthread_report(l);
#endif

	/* keep trying to find work until the polling timeout expires */
	last_tsc = rdtsc();
	if (!preempt_cede_needed(l) &&
	    (++iters < RUNTIME_SCHED_POLL_ITERS ||
	     last_tsc - start_tsc < cycles_per_us * RUNTIME_SCHED_MIN_POLL_US ||
	     storage_pending_completions(&l->storage_q))) {
		goto again;
	}

	l->parked = true;
	spin_unlock(&l->lock);

	/* did not find anything to run, park this kthread */
	STAT(SCHED_CYCLES) += last_tsc - start_tsc;
	/* we may have got a preempt signal before voluntarily yielding */
	kthread_park(!preempt_cede_needed(l));
	start_tsc = rdtsc();
	iters = 0;

	spin_lock(&l->lock);
	l->parked = false;
	goto again;

done:
	/* pop off a thread and run it */
	assert(l->rq_head != l->rq_tail);
	th = l->rq[l->rq_tail++ % RUNTIME_RQ_SIZE];
	ACCESS_ONCE(l->q_ptrs->rq_tail)++;
	l->curr_th = th;

	/* move overflow tasks into the runqueue */
	if (unlikely(!list_empty(&l->rq_overflow)))
		drain_overflow(l);

	update_oldest_tsc(l);
	spin_unlock(&l->lock);

	/* update exit stat counters */
	last_tsc = rdtsc();
	STAT(SCHED_CYCLES) += last_tsc - start_tsc;
	if (cores_have_affinity(th->last_cpu, l->curr_cpu))
		STAT(LOCAL_RUNS)++;
	else
		STAT(REMOTE_RUNS)++;

	/* update exported thread run start time */
	th->run_start_tsc = last_tsc;
	ACCESS_ONCE(l->q_ptrs->run_start_tsc) = last_tsc;

	/* increment the RCU generation number (odd is in thread) */
	store_release(&l->rcu_gen, l->rcu_gen + 1);
	ACCESS_ONCE(l->q_ptrs->rcu_gen) = l->rcu_gen;
	assert((l->rcu_gen & 0x1) == 0x1);

	/* and jump into the next thread */
	jmp_thread(th);
}

static inline struct aligned_cycles *get_aligned_cycles(void *owner_proclet)
{
	return (struct aligned_cycles *)owner_proclet;
}

static inline void __update_monitor_cycles(uint64_t now_tsc)
{
	thread_t *curth = thread_self();
	struct aligned_cycles *cycles;
	uint64_t run_start_tsc;

	if (curth->nu_state.monitor_cnt) {
		cycles = get_aligned_cycles(curth->nu_state.owner_proclet);
		if (cycles) {
			run_start_tsc = curth->run_start_tsc;
			if (likely(now_tsc > run_start_tsc)) {
				cycles[read_cpu()].c += now_tsc - run_start_tsc;
				curth->run_start_tsc = now_tsc;
			}
		}
	}
}

void update_monitor_cycles(void)
{
	__update_monitor_cycles(rdtsc());
}

static __always_inline void enter_schedule(thread_t *curth)
{
	struct kthread *k = myk();
	thread_t *th;
	uint64_t now_tsc;

	assert_preempt_disabled();

	now_tsc = rdtsc();
	__update_monitor_cycles(now_tsc);

	/* prepare current thread for sleeping */
	curth->last_cpu = k->curr_cpu;

	spin_lock(&k->lock);

	/* slow path: switch from the uthread stack to the runtime stack */
	if (k->rq_head == k->rq_tail ||
	    preempt_cede_needed(k) ||
	    has_pending_prioritize_req(k) ||
	    has_pending_pause_req(k) ||
	    k->preemptor->th ||
#ifdef GC
	    get_gc_gen() != k->local_gc_gen ||
#endif
	    (!disable_watchdog &&
	     unlikely(now_tsc - k->last_softirq_tsc >
		      cycles_per_us * RUNTIME_WATCHDOG_US))) {
		jmp_runtime(schedule);
		return;
	}

	/* fast path: switch directly to the next uthread */
	STAT(PROGRAM_CYCLES) += now_tsc - last_tsc;
	last_tsc = now_tsc;

	/* pop the next runnable thread from the queue */
	th = k->rq[k->rq_tail++ % RUNTIME_RQ_SIZE];
	ACCESS_ONCE(k->q_ptrs->rq_tail)++;
	k->curr_th = th;

	/* move overflow tasks into the runqueue */
	if (unlikely(!list_empty(&k->rq_overflow)))
		drain_overflow(k);

	update_oldest_tsc(k);
	spin_unlock(&k->lock);

	/* update exported thread run start time */
	th->run_start_tsc = last_tsc;
	ACCESS_ONCE(k->q_ptrs->run_start_tsc) = last_tsc;

	/* increment the RCU generation number (odd is in thread) */
	store_release(&k->rcu_gen, k->rcu_gen + 2);
	ACCESS_ONCE(k->q_ptrs->rcu_gen) = k->rcu_gen;
	assert((k->rcu_gen & 0x1) == 0x1);

	/* check for misuse of preemption disabling */
	BUG_ON((preempt_cnt & ~PREEMPT_NOT_PENDING) != 1);

	/* check if we're switching into the same thread as before */
	if (unlikely(th == curth)) {
		th->thread_ready = false;
		preempt_enable();
		return;
	}

	/* switch stacks and enter the next thread */
	STAT(RESCHEDULES)++;
	if (cores_have_affinity(th->last_cpu, k->curr_cpu))
		STAT(LOCAL_RUNS)++;
	else
		STAT(REMOTE_RUNS)++;
	jmp_thread_direct(curth, th);
}

/**
 * thread_park_and_unlock_np - puts a thread to sleep, unlocks the lock @l,
 *                             and schedules the next thread
 * @l: the lock to be released
 */
void thread_park_and_unlock_np(spinlock_t *l)
{
	thread_t *curth = thread_self();

	assert_preempt_disabled();
	assert_spin_lock_held(l);

	spin_unlock(l);
	enter_schedule(curth);
}

/**
 * thread_park_and_preempt_enable - puts a thread to sleep, enables preemption,
 *                                  and schedules the next thread
 */
void thread_park_and_preempt_enable(void)
{
	thread_t *curth = thread_self();

	assert_preempt_disabled();
	enter_schedule(curth);
}

static void thread_ready_prepare(struct kthread *k, thread_t *th)
{
	/* check for misuse where a ready thread is marked ready again */
	BUG_ON(th->thread_ready);

	/* prepare thread to be runnable */
	th->thread_ready = true;
	th->ready_tsc = rdtsc();
	if (cores_have_affinity(th->last_cpu, k->curr_cpu))
		STAT(LOCAL_WAKES)++;
	else
		STAT(REMOTE_WAKES)++;
}

/**
 * thread_ready_locked - makes a uthread runnable (at tail, kthread lock held)
 * @th: the thread to mark runnable
 *
 * This function can only be called when @th is parked.
 * This function must be called with preemption disabled and the kthread lock
 * held.
 */
void thread_ready_locked(thread_t *th)
{
	struct kthread *k = myk();

	assert_preempt_disabled();
	assert_spin_lock_held(&k->lock);

	thread_ready_prepare(k, th);
	if (unlikely(k->rq_head - k->rq_tail >= RUNTIME_RQ_SIZE)) {
		assert(k->rq_head - k->rq_tail == RUNTIME_RQ_SIZE);
		list_add_tail(&k->rq_overflow, &th->link);
		ACCESS_ONCE(k->q_ptrs->rq_head)++;
		STAT(RQ_OVERFLOW)++;
		return;
	}

	k->rq[k->rq_head++ % RUNTIME_RQ_SIZE] = th;
	if (k->rq_head - k->rq_tail == 1)
		ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
	ACCESS_ONCE(k->q_ptrs->rq_head)++;
}

/**
* thread_ready_head_locked - makes a uthread runnable (at head, kthread lock held)
 * @th: the thread to mark runnable
 *
 * This function can only be called when @th is parked.
 * This function must be called with preemption disabled and the kthread lock
 * held.
 */
void thread_ready_head_locked(thread_t *th, uint64_t ready_tsc)
{
	struct kthread *k = myk();
	thread_t *oldestth;

	assert_preempt_disabled();
	assert_spin_lock_held(&k->lock);

	thread_ready_prepare(k, th);

	if (k->rq_head != k->rq_tail)
		th->ready_tsc = k->rq[k->rq_tail % RUNTIME_RQ_SIZE]->ready_tsc;
	th->ready_tsc = MIN(th->ready_tsc, ready_tsc);
	oldestth = k->rq[--k->rq_tail % RUNTIME_RQ_SIZE];
	k->rq[k->rq_tail % RUNTIME_RQ_SIZE] = th;
	if (unlikely(k->rq_head - k->rq_tail > RUNTIME_RQ_SIZE)) {
		list_add(&k->rq_overflow, &oldestth->link);
		k->rq_head--;
		STAT(RQ_OVERFLOW)++;
	}
	ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
	ACCESS_ONCE(k->q_ptrs->rq_head)++;
}

/**
 * thread_ready_head - makes a uthread runnable (at the head of the queue)
 * @th: the thread to mark runnable
 *
 * This function can only be called when @th is parked.
 */
void thread_ready_head(thread_t *th)
{
	struct kthread *k;
	thread_t *oldestth;

	k = getk();
	thread_ready_prepare(k, th);
	spin_lock(&k->lock);
	if (k->rq_head != k->rq_tail)
		th->ready_tsc = k->rq[k->rq_tail % RUNTIME_RQ_SIZE]->ready_tsc;
	oldestth = k->rq[--k->rq_tail % RUNTIME_RQ_SIZE];
	k->rq[k->rq_tail % RUNTIME_RQ_SIZE] = th;
	if (unlikely(k->rq_head - k->rq_tail > RUNTIME_RQ_SIZE)) {
		list_add(&k->rq_overflow, &oldestth->link);
		k->rq_head--;
		STAT(RQ_OVERFLOW)++;
	}
	ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
	ACCESS_ONCE(k->q_ptrs->rq_head)++;
	spin_unlock(&k->lock);
	putk();
}

/**
 * thread_ready - makes a uthread runnable (at the tail of the queue)
 * @th: the thread to mark runnable
 *
 * This function can only be called when @th is parked.
 */
void thread_ready(thread_t *th)
{
	struct kthread *k;
	uint32_t rq_tail;

	k = getk();
	spin_lock(&k->lock);
	thread_ready_prepare(k, th);
	rq_tail = load_acquire(&k->rq_tail);

	if (unlikely(k->rq_head - rq_tail >= RUNTIME_RQ_SIZE)) {
		assert(k->rq_head - rq_tail == RUNTIME_RQ_SIZE);
		list_add_tail(&k->rq_overflow, &th->link);
		ACCESS_ONCE(k->q_ptrs->rq_head)++;
		spin_unlock(&k->lock);
		putk();
		STAT(RQ_OVERFLOW)++;
		return;
	}
	k->rq[k->rq_head % RUNTIME_RQ_SIZE] = th;
	store_release(&k->rq_head, k->rq_head + 1);
	if (k->rq_head - load_acquire(&k->rq_tail) == 1)
		ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
	ACCESS_ONCE(k->q_ptrs->rq_head)++;
	spin_unlock(&k->lock);
	putk();
}

static void thread_finish_cede(void)
{
	struct kthread *k = myk();
	thread_t *myth = thread_self();
	uint64_t tsc = rdtsc();

	/* update stats and scheduler state */
	myth->thread_running = false;
	myth->last_cpu = k->curr_cpu;
	__self = NULL;
	STAT(PROGRAM_CYCLES) += tsc - last_tsc;

	/* mark ceded thread ready at head of runqueue */
	spin_lock(&k->lock);
	k->curr_th = NULL;
	thread_ready_head_locked(myth, -1);
	spin_unlock(&k->lock);

	/* increment the RCU generation number (even - pretend in sched) */
	store_release(&k->rcu_gen, k->rcu_gen + 1);
	ACCESS_ONCE(k->q_ptrs->rcu_gen) = k->rcu_gen;
	assert((k->rcu_gen & 0x1) == 0x0);

	/* cede this kthread to the iokernel */
	ACCESS_ONCE(k->parked) = true; /* deliberately racy */
	kthread_park(false);
	last_tsc = tsc;

	/* increment the RCU generation number (odd - back in thread) */
	store_release(&k->rcu_gen, k->rcu_gen + 1);
	ACCESS_ONCE(k->q_ptrs->rcu_gen) = k->rcu_gen;
	assert((k->rcu_gen & 0x1) == 0x1);

	/* re-enter the scheduler */
	spin_lock(&k->lock);
	k->parked = false;
	schedule();
}

/**
 * thread_cede - yields the running thread and gives the core back to iokernel
 */
void thread_cede(void)
{
	assert_preempt_disabled();
	update_monitor_cycles();
	/* this will switch from the thread stack to the runtime stack */
	jmp_runtime(thread_finish_cede);
}

/**
 * thread_yield - yields the currently running thread
 *
 * Yielding will give other threads and softirqs a chance to run.
 */
void thread_yield(void)
{
	thread_t *curth;

	/* check for softirqs */
	softirq_run();

	preempt_disable();
	curth = thread_self();
	curth->thread_ready = false;
	thread_ready(curth);
	enter_schedule(curth);
}

void thread_yield_and_preempt_enable(void)
{
	thread_t *curth = thread_self();

	assert_preempt_disabled();
	softirq_run_preempt_disabled();
	curth->thread_ready = false;
	thread_ready(curth);
	enter_schedule(curth);
}

static __always_inline thread_t *__thread_create(void)
{
	struct thread *th;
	struct stack *s;

	preempt_disable();
	th = tcache_alloc(&perthread_get(thread_pt));
	if (unlikely(!th)) {
		preempt_enable();
		return NULL;
	}

	s = stack_alloc();
	if (unlikely(!s)) {
		tcache_free(&perthread_get(thread_pt), th);
		preempt_enable();
		return NULL;
	}
	th->last_cpu = myk()->curr_cpu;
	preempt_enable();

	th->stack = s;
	th->main_thread = false;
	th->thread_ready = false;
	th->thread_running = false;
	th->wq_spin = false;
	th->migrated = false;
	memset(th->nu_state.rcu_ctxs, 0, sizeof(th->nu_state.rcu_ctxs));
	th->nu_state.monitor_cnt = 0;
	th->nu_state.creator_ip = get_cfg_ip();
	th->nu_state.proclet_slab = NULL;
	th->nu_state.owner_proclet = NULL;

	return th;
}

/**
 * thread_create - creates a new thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
thread_t *thread_create(thread_fn_t fn, void *arg)
{
	thread_t *th = __thread_create();
	if (unlikely(!th))
		return NULL;

	th->nu_state.tf.rsp = stack_init_to_rsp(th->stack, thread_exit);
	th->nu_state.tf.rdi = (uint64_t)arg;
	/* just in case base pointers are enabled */
	th->nu_state.tf.rbp = (uint64_t)0;
	th->nu_state.tf.rip = (uint64_t)fn;
	gc_register_thread(th);
	return th;
}

/**
 * thread_create_with_buf - creates a new thread with space for a buffer on the
 * stack
 * @fn: a function pointer to the starting method of the thread
 * @buf: a pointer to the stack allocated buffer (passed as arg too)
 * @buf_len: the size of the stack allocated buffer
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
thread_t *thread_create_with_buf(thread_fn_t fn, void **buf, size_t buf_len)
{
	void *ptr;
	thread_t *th = __thread_create();
	if (unlikely(!th))
		return NULL;

	th->nu_state.tf.rsp = stack_init_to_rsp_with_buf(th->stack, &ptr, buf_len,
						         thread_exit);
	th->nu_state.tf.rdi = (uint64_t)ptr;
	/* just in case base pointers are enabled */
	th->nu_state.tf.rbp = (uint64_t)0;
	th->nu_state.tf.rip = (uint64_t)fn;
	*buf = ptr;
	gc_register_thread(th);
	return th;
}

thread_t *thread_nu_create_with_args(void *proclet_stack,
                                     uint32_t proclet_stack_size, thread_fn_t fn,
                                     void *args, bool copy_rcu_ctxs)
{
	thread_t *th = __thread_create();
	if (unlikely(!th))
		return NULL;

	th->nu_state.proclet_slab = __self->nu_state.proclet_slab;
	th->nu_state.monitor_cnt = __self->nu_state.monitor_cnt;
	th->nu_state.owner_proclet = __self->nu_state.owner_proclet;
	th->nu_state.tf.rsp = nu_stack_init_to_rsp(proclet_stack);
	th->nu_state.tf.rdi = (uint64_t)args;
	/* just in case base pointers are enabled */
	th->nu_state.tf.rbp = (uint64_t)0;
	th->nu_state.tf.rip = (uint64_t)fn;
	if (copy_rcu_ctxs)
		memcpy(th->nu_state.rcu_ctxs, __self->nu_state.rcu_ctxs,
			sizeof(__self->nu_state.rcu_ctxs));
	gc_register_thread(th);
	return th;
}

/**
 * thread_spawn - creates and launches a new thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
int thread_spawn(thread_fn_t fn, void *arg)
{
	thread_t *th = thread_create(fn, arg);
	if (unlikely(!th))
		return -ENOMEM;
	thread_ready(th);
	return 0;
}

/**
 * thread_spawn_main - creates and launches the main thread
 * @fn: a function pointer to the starting method of the thread
 * @arg: an argument passed to @fn
 *
 * WARNING: Only can be called once.
 *
 * Returns 0 if successful, otherwise -ENOMEM if out of memory.
 */
int thread_spawn_main(thread_fn_t fn, void *arg)
{
	static bool called = false;
	thread_t *th;

	BUG_ON(called);
	called = true;

	th = thread_create(fn, arg);
	if (!th)
		return -ENOMEM;
	th->main_thread = true;
	thread_ready(th);
	return 0;
}

static void thread_finish_exit(void)
{
	struct thread *th = thread_self();

	/* if the main thread dies, kill the whole program */
	if (unlikely(th->main_thread))
		init_shutdown(EXIT_SUCCESS);

	gc_remove_thread(th);
	stack_free(th->stack);
	tcache_free(&perthread_get(thread_pt), th);
	__self = NULL;

	spin_lock(&myk()->lock);
	myk()->curr_th = NULL;
	schedule();
}

/**
 * thread_exit - terminates a thread
 */
void thread_exit(void)
{
	/* can't free the stack we're currently using, so switch */
	preempt_disable();
	jmp_runtime_nosave(thread_finish_exit);
}

/**
 * immediately park each kthread when it first starts up, only schedule it once
 * the iokernel has granted it a core
 */
static __noreturn void schedule_start(void)
{
	struct kthread *k = myk();

	/*
	 * force kthread parking (iokernel assumes all kthreads are parked
	 * initially). Update RCU generation so it stays even after entering
	 * schedule().
	 */
	if (k->q_ptrs->oldest_tsc == 0)
		ACCESS_ONCE(k->q_ptrs->oldest_tsc) = UINT64_MAX;
	ACCESS_ONCE(k->parked) = true;
	kthread_wait_to_attach();
	last_tsc = rdtsc();
	store_release(&k->rcu_gen, 1);
	ACCESS_ONCE(k->q_ptrs->rcu_gen) = 1;

	spin_lock(&k->lock);
	k->parked = false;
	schedule();
}

/**
 * sched_start - used only to enter the runtime the first time
 */
void sched_start(void)
{
	preempt_disable();
	jmp_runtime_nosave(schedule_start);
}

static void runtime_top_of_stack(void)
{
	panic("a thread returned to the top of the stack");
}

/**
 * sched_init_thread - initializes per-thread state for the scheduler
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int sched_init_thread(void)
{
	struct stack *s;

	tcache_init_perthread(thread_tcache, &perthread_get(thread_pt));
	s = stack_alloc();
	if (!s)
		return -ENOMEM;

	runtime_stack_base = (void *)s;
	runtime_stack = (void *)stack_init_to_rsp(s, runtime_top_of_stack);

	return 0;
}

/**
 * sched_init - initializes the scheduler subsystem
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int sched_init(void)
{
	int ret, i, j, siblings;

	/*
	 * set up allocation routines for threads
	 */
	ret = slab_create(&thread_slab, "runtime_threads",
			  sizeof(struct thread), 0);
	if (ret)
		return ret;

	thread_tcache = slab_create_tcache(&thread_slab,
					   TCACHE_MAX_MAG_SIZE);
	if (!thread_tcache) {
		slab_destroy(&thread_slab);
		return -ENOMEM;
	}

	for (i = 0; i < cpu_count; i++) {
		siblings = 0;
		bitmap_for_each_set(cpu_info_tbl[i].thread_siblings_mask,
				    cpu_count, j) {
			if (i == j)
				continue;
			BUG_ON(siblings++);
			cpu_map[i].sibling_core = j;
		}
	}

	return 0;
}

bool thread_has_been_migrated(void)
{
	return __self->migrated;
}

void pause_migrating_ths_aux(void)
{
	int i, last = 0;
	bool done;

	while (!ACCESS_ONCE(global_pause_req_mask))
		;

retry:
	i = last;
	done = true;
	for (; i < maxks; i++) {
		if (ACCESS_ONCE(ks[i]->pause_req) &&
		    !handle_pending_pause_req(ks[i])) {
			if (done) {
				last = i;
				done = false;
			}
		} else if (!list_empty(&ks[i]->migrating_ths)) {
			list_append_list(&all_migrating_ths,
					 &ks[i]->migrating_ths);
		}
	}
	if (!done)
		goto retry;

	store_release(&global_pause_req_mask, false);
}

void pause_migrating_ths_main(void *owner_proclet)
{
	int i;
	cpu_set_t mask;
	struct kthread *k;
	uint64_t wait_start_us;
	bool intr_all_cores = false;

	for (i = 0; i < maxks; i++)
		ks[i]->pause_req = true;
	pause_req_owner_proclet = owner_proclet;
	store_release(&global_pause_req_mask, true);

	CPU_ZERO(&mask);
	for (i = 0; i < maxks; i++) {
		k = ks[i];
		if (ACCESS_ONCE(k->pause_req)) {
		        /*
		         * Intentionally don't grab the lock for performance.
		         * The (ultra rare) race condition will be handled
		         * by the fallback path below.
		         */
			if (!can_handle_pause_req(k))
				kthread_enqueue_yield_intr(&mask, k);
		}
	}
	kthread_send_yield_intrs(&mask);

	wait_start_us = microtime();
	while (ACCESS_ONCE(global_pause_req_mask)) {
		/* The fallback path. */
		if (!intr_all_cores) {
			if (microtime() - wait_start_us >=
			    RUNTIME_PAUSE_THS_MAX_WAIT_US) {
				kthread_yield_all_cores();
				intr_all_cores = true;
			}
		}
		cpu_relax();
	}
}

uint64_t thread_get_rsp(thread_t *th) { return th->nu_state.tf.rsp; }

void *thread_get_nu_state(thread_t *th, size_t *nu_state_size)
{
	*nu_state_size = sizeof(struct thread_nu_state);
	return &th->nu_state;
}

thread_t *restore_thread(void *nu_state)
{
	thread_t *th = __thread_create();
	BUG_ON(!th);
	th->nu_state = *(struct thread_nu_state *)nu_state;
	th->migrated = true;
	return th;
}

void gc_migrated_threads(void)
{
	thread_t *th;

	while ((th = list_pop(&all_migrating_ths, thread_t, link))) {
		stack_free(th->stack);
		tcache_free(&perthread_get(thread_pt), th);
	}
}

void *thread_get_runtime_stack_base(void)
{
	return &__self->stack->usable[STACK_PTR_SIZE];
}

void thread_flush_all_monitor_cycles(void)
{
       int i;
       thread_t *th;
       uint64_t curr_tsc = rdtsc(), run_start_tsc;
       struct aligned_cycles *cycles;

       for (i = 0; i < maxks; i++) {
              th = ks[i]->curr_th;
	      if (th && th->nu_state.monitor_cnt) {
                     cycles = get_aligned_cycles(th->nu_state.owner_proclet);
                     if (cycles) {
                            run_start_tsc = th->run_start_tsc;
                            if (likely(curr_tsc > run_start_tsc)) {
                                   cycles[0].c += curr_tsc - run_start_tsc;
                                   th->run_start_tsc = curr_tsc;
                            }
                     }
	      }
       }
}

int32_t thread_hold_rcu(void *rcu, bool flag)
{
       int i;
       struct rcu_context *ctx;

       for (i = 0; i < MAX_NUM_RCUS_HELD; i++) {
              ctx = &__self->nu_state.rcu_ctxs[i];
              if (ctx->rcu == rcu)
                     return ++ctx->nesting_cnt;
       }
       for (i = 0; i < MAX_NUM_RCUS_HELD; i++) {
              ctx = &__self->nu_state.rcu_ctxs[i];
              if (!ctx->rcu) {
                     ctx->rcu = rcu;
                     ctx->nesting_cnt = 1;
                     ctx->flag = flag;
                     return 1;
	      }
       }
       BUG();
}

int32_t thread_unhold_rcu(void *rcu, bool *flag)
{
       int i;
       struct rcu_context *ctx;

       for (i = 0; i < MAX_NUM_RCUS_HELD; i++) {
              ctx = &__self->nu_state.rcu_ctxs[i];
              if (ctx->rcu == rcu) {
                     if (--ctx->nesting_cnt == 0)
                            ctx->rcu = NULL;
                     *flag = ctx->flag;
                     return ctx->nesting_cnt;
              }
       }
       BUG();
}

inline bool thread_is_rcu_held(thread_t *th, void *rcu) {
       int i;

       for (i = 0; i < MAX_NUM_RCUS_HELD; i++) {
              if (th->nu_state.rcu_ctxs[i].rcu == rcu)
                     return true;
       }
       return false;
}

void prioritize_and_wait_rcu_readers(void *rcu)
{
	int i;

	for (i = 0; i < maxks; i++) {
		ks[i]->prioritize_req = true;
	}
	store_release(&global_prioritized_rcu, rcu);
	kthread_yield_all_cores();
        prioritize_local_rcu_readers();
retry:
	for (i = 0; i < maxks; i++)
		if (ACCESS_ONCE(ks[i]->prioritize_req) ||
		    !list_empty_volatile(&ks[i]->rq_deprioritized))
			goto retry;
	store_release(&global_prioritized_rcu, NULL);
}

uint32_t thread_get_creator_ip(void)
{
	return __self->nu_state.creator_ip;
}

void thread_wait_until_parked(thread_t *th)
{
	while (load_acquire(&th->thread_running))
		cpu_relax();
}

void prealloc_threads_and_stacks(uint32_t num_mags)
{
	tcache_reserve(thread_tcache, num_mags);
	tcache_reserve(stack_tcache, num_mags);
}

void unblock_spin(void)
{
	/* unblock ongoing prioritization */
	prioritize_local_rcu_readers();

	/* unblock ongoing migration */
	pause_local_migrating_threads();

	/* shed work to other threads */
	shed_work();

	/* respond to arp reqs */
	iokernel_softirq_poll(getk());
	putk();
}
