#ifndef MAYOS_SECURITY_ENTROPY_SEED_H
#define MAYOS_SECURITY_ENTROPY_SEED_H
#define MAY_ENTROPY_SEED_BYTES 64
/* The sampler zeroes its output on failure and exposes no fallback seed. */
int may_entropy_seed(unsigned char output[MAY_ENTROPY_SEED_BYTES]);
#endif
