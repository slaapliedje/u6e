/* rng.c — xorshift32 PRNG. Portable, deterministic. */
#include "rng.h"

void rng_seed(rng_t *r, uint32_t seed)
{
    r->state = seed ? seed : 0xA5A5A5A5u;   /* avoid the zero fixed point */
}

uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

int rng_range(rng_t *r, int n)
{
    if (n <= 1) return 0;
    return (int)(rng_next(r) % (uint32_t)n);
}
