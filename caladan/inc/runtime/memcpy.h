#pragma once

static inline void *memcpy_ermsb(void *__restrict dst,
				 const void *__restrict src, size_t n)
{
	asm volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n)::"memory");
	return dst;
}

extern void *memcpy_avx2_nt(void * __restrict dst, const void *__restrict src,
			    size_t size);
