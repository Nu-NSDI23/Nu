#include <stdint.h>
#include <sys/sysinfo.h>

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
	       !bitmap_test(sd->reserved_rp_cores, th->core);
}

static void ias_ps_preempt_core(struct ias_data *sd)
{
	unsigned int i, num_needed_cores = *sd->p->num_resource_pressure_handlers;
	struct thread *selected[num_needed_cores];
	unsigned int num_selected = 0;
	struct thread *th;
	uint64_t now_tsc;
	bool is_lc = sd->is_lc;

	sd->is_lc = true;
	for (i = 0;
	     i < sd->p->active_thread_count && num_selected < num_needed_cores;
	     i++) {
		th = sd->p->active_threads[i];
		if (is_eligible(sd, th))
			selected[num_selected++] = th;
	}

	while (num_selected < num_needed_cores) {
		if (unlikely(ias_add_kthread(sd) != 0))
			goto done;
		selected[num_selected++] =
			sd->p->active_threads[sd->p->active_thread_count - 1];
	}

	sd->p->resource_pressure_info->last_us = now_us;
	sd->p->resource_pressure_info->status = HANDLING;
	mb();
	if (unlikely(!ACCESS_ONCE(*sd->p->num_resource_pressure_handlers))) {
		sd->p->resource_pressure_info->status = NONE;
		goto done;
	}

	now_tsc = rdtsc();
	BUG_ON(num_selected != num_needed_cores);
	for (i = 0; i < num_selected; i++) {
		th = selected[i];
		BUG_ON(th->preemptor->th);
		th->preemptor->th =
                        sd->p->resource_pressure_handlers[--num_needed_cores];
		th->preemptor->ready_tsc = now_tsc;
		barrier();
		/* Grant exclusive access by marking the core as reserved. */
		bitmap_set(sd->reserved_ps_cores, th->core);
		sched_yield_on_core(th->core);
	}

done:
	sd->is_lc = is_lc;
}

void ias_ps_poll(void)
{
	bool has_pressure;
	struct sysinfo info;
	int64_t free_ram_mbs, used_swap_mbs, to_release_mem_mbs;
	struct congestion_info *congestion;
	struct resource_pressure_info *pressure;
	struct ias_data *sd;
	int pos;

	BUG_ON(sysinfo(&info) != 0);
	used_swap_mbs = (info.totalswap - info.freeswap) / SIZE_MB;
	free_ram_mbs = (int64_t)info.freeram / SIZE_MB - used_swap_mbs;
	if (free_ram_mbs < IAS_PS_MEM_LOW_MB)
		to_release_mem_mbs = IAS_PS_MEM_LOW_MB - free_ram_mbs;
	else
		to_release_mem_mbs = 0;

	ias_for_each_proc(sd) {
		pressure = sd->p->resource_pressure_info;
		congestion = sd->p->congestion_info;
		congestion->free_mem_mbs = MAX(0, free_ram_mbs);
		congestion->idle_num_cores = ias_num_idle_cores;
		has_pressure = false;

		if (pressure->mock) {
			pressure->to_release_mem_mbs = INT_MAX;
			has_pressure = true;
			goto update_fsm;
		}

		/* Memory pressure. */
		if (sd->react_mem_pressure) {
			pressure->to_release_mem_mbs =
				to_release_mem_mbs;
			has_pressure = to_release_mem_mbs;
		}

		/* CPU pressure. */
		if (sd->react_cpu_pressure) {
			if (sd->is_congested) {
				if (!sd->cpu_pressure_start_us)
					sd->cpu_pressure_start_us = now_us;
				else if (now_us - sd->cpu_pressure_start_us >=
					 IAS_PS_CPU_THRESH_US) {
					pressure->cpu_pressure = true;
					has_pressure = true;
				}
			} else {
				sd->cpu_pressure_start_us = 0;
				pressure->cpu_pressure = false;
			}
		}

	update_fsm:
		if (pressure->status == HANDLED) {
	                /* Take away the exclusive access. */
	                bitmap_for_each_set(sd->reserved_ps_cores, NCPU, pos)
				bitmap_clear(sd->reserved_ps_cores, pos);
			pressure->status = NONE;
		}

		if (pressure->status == NONE && has_pressure)
			if (pressure->last_us + IAS_PS_INTERVAL_US < now_us)
                                ias_ps_preempt_core(sd);
        }
}
