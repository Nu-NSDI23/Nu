/*
 * sync.h - support for synchronization
 */

#pragma once

#include <base/list.h>
#include <base/lock.h>
#include <base/stddef.h>
#include <base/time.h>
#include <runtime/preempt.h>
#include <runtime/thread.h>

/*
 * Mutex support
 */

struct mutex {
	atomic_t		held;
	spinlock_t		waiter_lock;
	struct list_head	waiters;
};

typedef struct mutex mutex_t;

extern void __mutex_lock(mutex_t *m);
extern void __mutex_unlock(mutex_t *m);
extern void mutex_init(mutex_t *m);

/**
 * mutex_try_lock - attempts to acquire a mutex
 * @m: the mutex to acquire
 *
 * Returns true if the acquire was successful.
 */
static inline bool mutex_try_lock(mutex_t *m)
{
	return atomic_cmpxchg(&m->held, 0, 1);
}

/**
 * mutex_lock - acquires a mutex
 * @m: the mutex to acquire
 */
static inline void mutex_lock(mutex_t *m)
{
	if (likely(atomic_cmpxchg(&m->held, 0, 1)))
		return;

	__mutex_lock(m);
}

/**
 * mutex_unlock - releases a mutex
 * @m: the mutex to release
 */
static inline void mutex_unlock(mutex_t *m)
{
	if (likely(atomic_cmpxchg(&m->held, 1, 0)))
		return;

	__mutex_unlock(m);
}
/**
 * mutex_held - is the mutex currently held?
 * @m: the mutex to check
 */
static inline bool mutex_held(const mutex_t *m)
{
	return atomic_read(&m->held);
}

/**
 * assert_mutex_held - asserts that a mutex is currently held
 * @m: the mutex that must be held
 */
static inline void assert_mutex_held(mutex_t *m)
{
	assert(mutex_held(m));
}

/*
 * Timed mutex support
 */

struct timed_mutex {
	atomic_t		held;
	spinlock_t		waiter_lock;
	struct list_head	waiters;
};

typedef struct timed_mutex timed_mutex_t;

extern void __timed_mutex_lock(timed_mutex_t *m);
extern void __timed_mutex_unlock(timed_mutex_t *m);
extern bool __timed_mutex_try_lock_until(timed_mutex_t *m,
                                         uint64_t deadline_us);
extern void timed_mutex_init(timed_mutex_t *m);

/**
 * timed_mutex_try_lock - attempts to acquire a timed mutex
 * @m: the timed mutex to acquire
 *
 * Returns true if the acquire was successful.
 */
static inline bool timed_mutex_try_lock(timed_mutex_t *m)
{
	return atomic_cmpxchg(&m->held, 0, 1);
}

static inline bool __timed_mutex_try_lock_for(timed_mutex_t *m,
                                              uint64_t duration_us)
{
	return __timed_mutex_try_lock_until(m, microtime() + duration_us);
}

/**
 * timed_mutex_try_lock_for - attempts to acquire a timed mutex. Blocks until
 * specified duration has elapsed or the lock is acquired, whichever comes
 * first.
 * @m: the timed mutex to acquire
 * @duration_us: the duration in microseconds.
 *
 * Returns true if the acquire was successful.
 */
static inline bool timed_mutex_try_lock_for(timed_mutex_t *m,
                                            uint64_t duration_us)
{
	if (timed_mutex_try_lock(m)) {
		return true;
	}
	return __timed_mutex_try_lock_for(m, duration_us);
}

/**
 * timed_mutex_try_lock_until - attempts to acquire a timed mutex. Blocks until
 * specified deadline has been reached or the lock is acquired, whichever comes
 * first.
 * @m: the timed mutex to acquire
 * @deadline_us: the deadline in microseconds.
 *
 * Returns true if the acquire was successful.
 */
static inline bool timed_mutex_try_lock_until(timed_mutex_t *m,
                                              uint64_t deadline_us)
{
	if (timed_mutex_try_lock(m)) {
		return true;
	}
	return __timed_mutex_try_lock_until(m, deadline_us);
}

/**
 * timed_mutex_lock - acquires a timed mutex
 * @m: the timed mutex to acquire
 */
static inline void timed_mutex_lock(timed_mutex_t *m)
{
	if (likely(atomic_cmpxchg(&m->held, 0, 1)))
		return;

	__timed_mutex_lock(m);
}

/**
 * timed_mutex_unlock - releases a timed mutex
 * @m: the timed mutex to release
 */
static inline void timed_mutex_unlock(timed_mutex_t *m)
{
	if (likely(atomic_cmpxchg(&m->held, 1, 0)))
		return;

	__timed_mutex_unlock(m);
}

/**
 * timed_mutex_held - is the timed mutex currently held?
 * @m: the timed mutex to check
 */
static inline bool timed_mutex_held(timed_mutex_t *m)
{
	return atomic_read(&m->held);
}

/**
 * assert_timed_mutex_held - asserts that a timed mutex is currently held
 * @m: the timed mutex that must be held
 */
static inline void assert_timed_mutex_held(timed_mutex_t *m)
{
	assert(timed_mutex_held(m));
}


/*
 * Condition variable support
 */

struct condvar {
	spinlock_t		waiter_lock;
	struct list_head	waiters;
};

typedef struct condvar condvar_t;

extern void condvar_wait(condvar_t *cv, mutex_t *m);
extern void condvar_signal(condvar_t *cv);
extern void condvar_broadcast(condvar_t *cv);
extern void condvar_init(condvar_t *cv);

/*
 * A condition variable variant that supports wait_for() and wait_until().
 */

struct timed_condvar {
	spinlock_t		waiter_lock;
	struct list_head	waiters;
};

typedef struct timed_condvar timed_condvar_t;

extern void timed_condvar_wait(timed_condvar_t *cv, timed_mutex_t *m);
extern bool timed_condvar_wait_until(timed_condvar_t *cv, timed_mutex_t *m,
                                     uint64_t deadline_us);
extern void timed_condvar_signal(timed_condvar_t *cv);
extern void timed_condvar_broadcast(timed_condvar_t *cv);
extern void timed_condvar_init(timed_condvar_t *cv);

/**
 * timed_condvar_wait_for - causes the current thread to block until the
 * condition variable is notified, a specific duration elapsed reached, or a
 * spurious wakeup occurs.
 *
 * @cv: the condition variable to signal
 * @m: the currently held mutex that projects the condition
 * @duration_us: the duration in microsecond.
 *
 * Returns false if the duration has been elapsed. Otherwise, returns true.
 */
static inline bool timed_condvar_wait_for(timed_condvar_t *cv, timed_mutex_t *m,
                                          uint64_t duration_us)
{
	return timed_condvar_wait_until(cv, m, microtime() + duration_us);
}

/*
 * Wait group support
 */

struct waitgroup {
	spinlock_t		lock;
	int			cnt;
	struct list_head	waiters;
};

typedef struct waitgroup waitgroup_t;

extern void waitgroup_add(waitgroup_t *wg, int cnt);
extern void waitgroup_wait(waitgroup_t *wg);
extern void waitgroup_init(waitgroup_t *wg);

/**
 * waitgroup_done - notifies the wait group that one waiting event completed
 * @wg: the wait group to complete
 */
static inline void waitgroup_done(waitgroup_t *wg)
{
	waitgroup_add(wg, -1);
}


/*
 * Spin lock support
 */

/**
 * spin_lock_np - takes a spin lock and disables preemption
 * @l: the spin lock
 */
static inline void spin_lock_np(spinlock_t *l)
{
	preempt_disable();
	spin_lock(l);
}

/**
 * spin_try_lock_np - takes a spin lock if its available and disables preemption
 * @l: the spin lock
 *
 * Returns true if successful, otherwise fail.
 */
static inline bool spin_try_lock_np(spinlock_t *l)
{
	preempt_disable();
	if (spin_try_lock(l))
		return true;

	preempt_enable();
	return false;
}

/**
 * spin_unlock_np - releases a spin lock and re-enables preemption
 * @l: the spin lock
 */
static inline void spin_unlock_np(spinlock_t *l)
{
	spin_unlock(l);
	preempt_enable();
}


/*
 * Barrier support
 */

struct barrier {
	spinlock_t		lock;
	int			waiting;
	int			count;
	struct list_head	waiters;
};

typedef struct barrier barrier_t;

extern void barrier_init(barrier_t *b, int count);
extern bool barrier_wait(barrier_t *b);


/*
 * Read-write mutex support
 */

struct rwmutex {
	spinlock_t		waiter_lock;
	int			count;
	struct list_head	read_waiters;
	struct list_head	write_waiters;
	int			read_waiter_count;
};

typedef struct rwmutex rwmutex_t;

extern void rwmutex_init(rwmutex_t *m);
extern void rwmutex_rdlock(rwmutex_t *m);
extern void rwmutex_wrlock(rwmutex_t *m);
extern bool rwmutex_try_rdlock(rwmutex_t *m);
extern bool rwmutex_try_wrlock(rwmutex_t *m);
extern void rwmutex_unlock(rwmutex_t *m);
