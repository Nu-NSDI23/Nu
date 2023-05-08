#include <immintrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "runtime/memcpy.h"

#define ALIGNMENT 32

#ifdef NDEBUG

static inline void __memcpy_avx2_256_nt(void **dst, const void **src,
                                        size_t *size, bool src_unaligned) {
        const __m256i *m_src;
        __m256i *m_dst;
        __m256i c0, c1, c2, c3, c4, c5, c6, c7;

        if (src_unaligned) {
                for (; *size >= 256; *size -= 256) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_loadu_si256(m_src + 0);
                        c1 = _mm256_loadu_si256(m_src + 1);
                        c2 = _mm256_loadu_si256(m_src + 2);
                        c3 = _mm256_loadu_si256(m_src + 3);
                        c4 = _mm256_loadu_si256(m_src + 4);
                        c5 = _mm256_loadu_si256(m_src + 5);
                        c6 = _mm256_loadu_si256(m_src + 6);
                        c7 = _mm256_loadu_si256(m_src + 7);
                        *src += 256;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        _mm256_stream_si256(m_dst + 2, c2);
                        _mm256_stream_si256(m_dst + 3, c3);
                        _mm256_stream_si256(m_dst + 4, c4);
                        _mm256_stream_si256(m_dst + 5, c5);
                        _mm256_stream_si256(m_dst + 6, c6);
                        _mm256_stream_si256(m_dst + 7, c7);
                        *dst += 256;
                }
        } else {
                for (; *size >= 256; *size -= 256) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_stream_load_si256(m_src + 0);
                        c1 = _mm256_stream_load_si256(m_src + 1);
                        c2 = _mm256_stream_load_si256(m_src + 2);
                        c3 = _mm256_stream_load_si256(m_src + 3);
                        c4 = _mm256_stream_load_si256(m_src + 4);
                        c5 = _mm256_stream_load_si256(m_src + 5);
                        c6 = _mm256_stream_load_si256(m_src + 6);
                        c7 = _mm256_stream_load_si256(m_src + 7);
                        *src += 256;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        _mm256_stream_si256(m_dst + 2, c2);
                        _mm256_stream_si256(m_dst + 3, c3);
                        _mm256_stream_si256(m_dst + 4, c4);
                        _mm256_stream_si256(m_dst + 5, c5);
                        _mm256_stream_si256(m_dst + 6, c6);
                        _mm256_stream_si256(m_dst + 7, c7);
                        *dst += 256;
                }
        }
}

static inline void __memcpy_avx2_128_nt(void **dst, const void **src,
                                        size_t *size, bool src_unaligned) {
        const __m256i *m_src;
        __m256i *m_dst;
        __m256i c0, c1, c2, c3;

        if (src_unaligned) {
                for (; *size >= 128; *size -= 128) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_loadu_si256(m_src + 0);
                        c1 = _mm256_loadu_si256(m_src + 1);
                        c2 = _mm256_loadu_si256(m_src + 2);
                        c3 = _mm256_loadu_si256(m_src + 3);
                        *src += 128;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        _mm256_stream_si256(m_dst + 2, c2);
                        _mm256_stream_si256(m_dst + 3, c3);
                        *dst += 128;
                }
        } else {
                for (; *size >= 128; *size -= 128) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_stream_load_si256(m_src + 0);
                        c1 = _mm256_stream_load_si256(m_src + 1);
                        c2 = _mm256_stream_load_si256(m_src + 2);
                        c3 = _mm256_stream_load_si256(m_src + 3);
                        *src += 128;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        _mm256_stream_si256(m_dst + 2, c2);
                        _mm256_stream_si256(m_dst + 3, c3);
                        *dst += 128;
                }
        }
}

static inline void __memcpy_avx2_64_nt(void **dst, const void **src,
                                       size_t *size, bool src_unaligned) {
        const __m256i *m_src;
        __m256i *m_dst;
        __m256i c0, c1;

        if (src_unaligned) {
                for (; *size >= 64; *size -= 64) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_loadu_si256(m_src + 0);
                        c1 = _mm256_loadu_si256(m_src + 1);
                        *src += 64;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        *dst += 64;
                }
        } else {
                for (; *size >= 64; *size -= 64) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_stream_load_si256(m_src + 0);
                        c1 = _mm256_stream_load_si256(m_src + 1);
                        *src += 64;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        _mm256_stream_si256(m_dst + 1, c1);
                        *dst += 64;
                }
        }
}

static inline void __memcpy_avx2_32_nt(void **dst, const void **src,
                                       size_t *size, bool src_unaligned) {
        const __m256i *m_src;
        __m256i *m_dst;
        __m256i c0;

        if (src_unaligned) {
                for (; *size >= 32; *size -= 32) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_loadu_si256(m_src + 0);
                        *src += 32;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        *dst += 32;
                }
        } else {
                for (; *size >= 32; *size -= 32) {
                        m_src = (const __m256i *)*src;
                        c0 = _mm256_stream_load_si256(m_src + 0);
                        *src += 32;

                        m_dst = (__m256i *)*dst;
                        _mm256_stream_si256(m_dst + 0, c0);
                        *dst += 32;
                }
        }
}

void *memcpy_avx2_nt(void *dst, const void *src, size_t size) {
        void *origin_dst = dst;
        size_t padding;
        bool src_unaligned;
        __m256i head;

        if (size < ALIGNMENT) {
                memcpy_ermsb(dst, src, size);
                return origin_dst;
        }

        // Try to align dst.
        padding =
            (ALIGNMENT - (((size_t)dst) & (ALIGNMENT - 1))) & (ALIGNMENT - 1);
        if (padding) {
                head = _mm256_loadu_si256((const __m256i *)src);
                _mm256_storeu_si256((__m256i *)dst, head);
                dst += padding;
                src += padding;
                size -= padding;
        }

        src_unaligned = (((size_t)src) & (ALIGNMENT - 1));
        __memcpy_avx2_256_nt(&dst, &src, &size, src_unaligned);
        __memcpy_avx2_128_nt(&dst, &src, &size, src_unaligned);
        __memcpy_avx2_64_nt(&dst, &src, &size, src_unaligned);
        __memcpy_avx2_32_nt(&dst, &src, &size, src_unaligned);

        if (size) {
                memcpy_ermsb(dst, src, size);
        }

        _mm256_zeroupper();
        _mm_sfence();

        return origin_dst;
}

#else

void *memcpy_avx2_nt(void *dst, const void *src, size_t size) {
  return memcpy(dst, src, size);
}

#endif
