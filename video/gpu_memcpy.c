#include <inttypes.h>
#include <string.h>
#include "gpu_memcpy.h"

#ifdef __x86_64__
typedef struct { uint8_t x[256]; } chunk;
#define ALL_XMM_REGS \
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
#else
typedef struct { uint8_t x[128]; } chunk;
#define ALL_XMM_REGS \
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
#endif

typedef struct { uint8_t x[64]; } cache_line;

void *gpu_memcpy(void *restrict d, const void *restrict s, size_t size)
{
    const uint8_t *src = s;
    uint8_t *dest = d;

    // Align src to a 64-byte cache line boundary
    if ((size_t)src & (sizeof(cache_line)-1)) {
        size_t pad = sizeof(cache_line) - ((size_t)src & (sizeof(cache_line)-1));
        memcpy(dest, src, pad);
        src += pad;
        dest += pad;
        size -= pad;
    }

    // Copy using all SSE registers
    while (size >= sizeof(chunk)) {
        // Probably not necessary, but this should tell GCC the size of the
        // memory clobbered by the __asm__ block
        const chunk *src_chunk = (const chunk*)src;
        chunk *dest_chunk = (chunk*)dest;

        __asm__(
            "movntdqa     (%[src]), %%xmm0\n\t"
            "movntdqa 0x10(%[src]), %%xmm1\n\t"
            "movntdqa 0x20(%[src]), %%xmm2\n\t"
            "movntdqa 0x30(%[src]), %%xmm3\n\t"
            "movntdqa 0x40(%[src]), %%xmm4\n\t"
            "movntdqa 0x50(%[src]), %%xmm5\n\t"
            "movntdqa 0x60(%[src]), %%xmm6\n\t"
            "movntdqa 0x70(%[src]), %%xmm7\n\t"
#ifdef __x86_64__
            "movntdqa 0x80(%[src]), %%xmm8\n\t"
            "movntdqa 0x90(%[src]), %%xmm9\n\t"
            "movntdqa 0xa0(%[src]), %%xmm10\n\t"
            "movntdqa 0xb0(%[src]), %%xmm11\n\t"
            "movntdqa 0xc0(%[src]), %%xmm12\n\t"
            "movntdqa 0xd0(%[src]), %%xmm13\n\t"
            "movntdqa 0xe0(%[src]), %%xmm14\n\t"
            "movntdqa 0xf0(%[src]), %%xmm15\n\t"
#endif
            "movdqu %%xmm0,      (%[dest])\n\t"
            "movdqu %%xmm1,  0x10(%[dest])\n\t"
            "movdqu %%xmm2,  0x20(%[dest])\n\t"
            "movdqu %%xmm3,  0x30(%[dest])\n\t"
            "movdqu %%xmm4,  0x40(%[dest])\n\t"
            "movdqu %%xmm5,  0x50(%[dest])\n\t"
            "movdqu %%xmm6,  0x60(%[dest])\n\t"
            "movdqu %%xmm7,  0x70(%[dest])\n\t"
#ifdef __x86_64__
            "movdqu %%xmm8,  0x80(%[dest])\n\t"
            "movdqu %%xmm9,  0x90(%[dest])\n\t"
            "movdqu %%xmm10, 0xa0(%[dest])\n\t"
            "movdqu %%xmm11, 0xb0(%[dest])\n\t"
            "movdqu %%xmm12, 0xc0(%[dest])\n\t"
            "movdqu %%xmm13, 0xd0(%[dest])\n\t"
            "movdqu %%xmm14, 0xe0(%[dest])\n\t"
            "movdqu %%xmm15, 0xf0(%[dest])\n\t"
#endif
            : "=m"(*dest_chunk)
            : "m"(*src_chunk), [dest] "r"(dest_chunk), [src] "r"(src_chunk)
            : ALL_XMM_REGS);

        src += sizeof(chunk);
        dest += sizeof(chunk);
        size -= sizeof(chunk);
    }

    // Copy remaining data in cache-line-sized blocks
    while (size >= sizeof(cache_line)) {
        const cache_line *src_cl = (const cache_line*)src;
        cache_line *dest_cl = (cache_line*)dest;

        __asm__(
            "movntdqa     (%[src]), %%xmm0\n\t"
            "movntdqa 0x10(%[src]), %%xmm1\n\t"
            "movntdqa 0x20(%[src]), %%xmm2\n\t"
            "movntdqa 0x30(%[src]), %%xmm3\n\t"
            "movdqu %%xmm0,     (%[dest])\n\t"
            "movdqu %%xmm1, 0x10(%[dest])\n\t"
            "movdqu %%xmm2, 0x20(%[dest])\n\t"
            "movdqu %%xmm3, 0x30(%[dest])\n\t"
            : "=m"(*dest_cl)
            : "m"(*src_cl), [dest] "r"(dest_cl), [src] "r"(src_cl)
            : "xmm0", "xmm1", "xmm2", "xmm3");

        src += sizeof(cache_line);
        dest += sizeof(cache_line);
        size -= sizeof(cache_line);
    }

    // Copy remaining data
    memcpy(dest, src, size);

    // memcpy must return the destination argument
    return d;
}
