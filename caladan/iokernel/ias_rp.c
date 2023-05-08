#include <stdint.h>

#include <base/hash.h>
#include <base/log.h>
#include <base/stddef.h>

#include "defs.h"
#include "ias.h"
#include "ksched.h"
#include "sched.h"

static bool is_eligible(struct ias_data *sd, struct thread *th)
{
	return th == sched_get_thread_on_core(th->core) &&
	       !th->preemptor->th &&
	       !bitmap_test(sd->reserved_ps_cores, th->core);
}

static void ias_rp_preempt_core(struct ias_data *sd, uint64_t now_tsc)
{
	unsigned int i;
	struct thread *th;
	bool is_lc = sd->is_lc;

	sd->is_lc = true;
	for (i = 0; i < sd->p->active_thread_count; i++)
		if (is_eligible(sd, sd->p->active_threads[i]))
		        break;

	if (unlikely(i == sd->p->active_thread_count)) {
		if (unlikely(ias_add_kthread(sd) != 0))
			goto done;
	}

	sd->p->resource_reporting->last_tsc = now_tsc;
	sd->p->resource_reporting->status = HANDLING;
	barrier();
	th = sd->p->active_threads[i];
	BUG_ON(th->preemptor->th);
	th->preemptor->th = sd->p->resource_reporting->handler;
	th->preemptor->ready_tsc = now_tsc;
	barrier();
	/* Grant exclusive access by marking the core as reserved. */
	bitmap_set(sd->reserved_rp_cores, th->core);
	sched_yield_on_core(th->core);

done:
	sd->is_lc = is_lc;
}

void ias_rp_poll(void)
{
	struct ias_data *sd;
	int pos;
	struct resource_reporting *report;
	uint64_t now_tsc = rdtsc();

	ias_for_each_proc(sd) {
		report = sd->p->resource_reporting;
		if (report->status == HANDLED) {
                        /* Take away the exclusive access. */
                        bitmap_for_each_set(sd->reserved_rp_cores, NCPU, pos)
                                bitmap_clear(sd->reserved_rp_cores, pos);
			report->status = NONE;
		}

		if (report->status == NONE)
                        if (report->handler &&
                            report->last_tsc +
                            IAS_RP_INTERVAL_US * cycles_per_us < now_tsc)
                                ias_rp_preempt_core(sd, now_tsc);
        }
}
