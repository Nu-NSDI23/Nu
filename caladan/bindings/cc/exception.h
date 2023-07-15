#pragma once

extern "C" int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t,
                                               void *),
                               void *data);

__attribute__((__used__)) static auto *dl_iterate_phdr_keeper =
    &dl_iterate_phdr;
