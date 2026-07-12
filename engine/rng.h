/* rng.h — deterministic PRNG seam (M5).
 * A single seedable generator so logic tests are reproducible (fixed seed →
 * fixed sequence). All engine randomness goes through this, never rand(). */
#ifndef U6E_RNG_H
#define U6E_RNG_H

#include <stdint.h>

typedef struct { uint32_t state; } rng_t;

void     rng_seed(rng_t *r, uint32_t seed);
uint32_t rng_next(rng_t *r);          /* full 32-bit */
int      rng_range(rng_t *r, int n);  /* 0..n-1 (n>0) */

#endif
