/* The subset of libgcc that gcc -m32 emits calls to (no 32-bit libgcc is
 * installed with the x86_64-only toolchain). */
#include <stdint.h>

static uint64_t udivmod(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    if (d == 0) { volatile int z = 0; return (uint64_t)(1 / z); }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d) { return udivmod(n, d, 0); }
uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r = 0; udivmod(n, d, &r); return r; }

int64_t __divdi3(int64_t n, int64_t d)
{
    int neg = (n < 0) ^ (d < 0);
    uint64_t q = udivmod(n < 0 ? -(uint64_t)n : (uint64_t)n,
                         d < 0 ? -(uint64_t)d : (uint64_t)d, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t n, int64_t d)
{
    uint64_t r = 0;
    udivmod(n < 0 ? -(uint64_t)n : (uint64_t)n,
            d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
    return n < 0 ? -(int64_t)r : (int64_t)r;
}
