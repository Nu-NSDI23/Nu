#include <sys/ioctl.h>

#define __user
#include "defs.h"
#include "../ksched/ksched.h"

extern int ksched_fd;

void membarrier()
{
	int i;
	cpu_set_t mask;
	struct ksched_runtime_intr_req req;
	struct kthread *k = getk();
	uint64_t last_core = k->curr_cpu;
	ssize_t s;

	CPU_ZERO(&mask);
	for (i = 0; i < maxks; i++)
		kthread_enqueue_intr(&mask, ks[i]);

	req.opcode = RUNTIME_INTR_MB;
	req.wait = true;
	req.len = sizeof(mask);
	req.mask = &mask;

	s = ioctl(ksched_fd, KSCHED_IOC_RUNTIME_INTR, &req);
	BUG_ON(s < 0);
	k->curr_cpu = s;
	if (k->curr_cpu != last_core)
		STAT(CORE_MIGRATIONS)++;
	store_release(&cpu_map[s].recent_kthread, k);
	putk();
}
