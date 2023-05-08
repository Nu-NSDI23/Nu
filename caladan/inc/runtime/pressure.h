#include <iokernel/control.h>

typedef void (*resource_pressure_fn)(void *args);
struct resource_pressure_closure {
	resource_pressure_fn fn;
	void *args;
};

extern void create_resource_pressure_handlers(
	struct resource_pressure_closure *closures,
	int num_closures);
extern void remove_all_resource_pressure_handlers(void);
