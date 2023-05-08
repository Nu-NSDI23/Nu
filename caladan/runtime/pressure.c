#include <runtime/pressure.h>

#include "defs.h"

/* real-time resource pressure signals (shared with the iokernel) */
struct resource_pressure_info *resource_pressure_info;
/* number of resource pressure handlers */
uint8_t *num_resource_pressure_handlers;
/* the pressure handlers */
struct thread **resource_pressure_handlers;

static void thread_wrapper(void *args)
{
	struct resource_pressure_closure *closure =
		(struct resource_pressure_closure *)args;

	while (true) {
		closure->fn(closure->args);
		thread_park_and_preempt_enable();
	}
}

void create_resource_pressure_handlers(
	struct resource_pressure_closure *closures,
	int num_closures)
{
	struct resource_pressure_closure *closure;
	int i;

	for (i = 0; i < num_closures; i++) {
		resource_pressure_handlers[i] =
			thread_create_with_buf(thread_wrapper, (void **)&closure,
					       sizeof(struct resource_pressure_closure));
		*closure = closures[i];
	}
	*num_resource_pressure_handlers = num_closures;
	store_release(&resource_pressure_info->status, NONE);
}

void remove_all_resource_pressure_handlers(void)
{
	*num_resource_pressure_handlers = 0;
}
