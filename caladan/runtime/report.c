#include <asm/ops.h>
#include <base/time.h>
#include <runtime/report.h>

struct resource_reporting *resource_reporting;

void set_resource_reporting_handler(thread_t *handler) {
	resource_reporting->handler = handler;
	store_release(&resource_reporting->status, NONE);
}

void finish_resource_reporting(void) {
	resource_reporting->status = HANDLED;
	resource_reporting->last_tsc = rdtsc();
}
