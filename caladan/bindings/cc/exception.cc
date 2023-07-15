#include <dlfcn.h>

#include "sync.h"

using DlIteratePhdrFn = int (*)(int (*)(struct dl_phdr_info *, size_t, void *),
                                void *);

static inline DlIteratePhdrFn get_real_dl_iterate_phdr() {
  static DlIteratePhdrFn real_dl_iterate_phdr;

  if (unlikely(!real_dl_iterate_phdr)) {
    real_dl_iterate_phdr =
        reinterpret_cast<DlIteratePhdrFn>(dlsym(RTLD_NEXT, "dl_iterate_phdr"));
  }
  return real_dl_iterate_phdr;
}

extern "C" int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t,
                                               void *),
                               void *data) {
  rt::Preempt p;
  rt::PreemptGuard g(&p);

  return get_real_dl_iterate_phdr()(callback, data);
}
