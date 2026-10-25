// The random generator and seed hashing.

#include "test.h"
#include "rng.h"

void run_rng_tests(void)
{
    Rng a, b;

    // Same seed, same sequence.
    rng_seed(&a, 1234);
    rng_seed(&b, 1234);
    for (int i = 0; i < 100; i++)
        CHECK_EQ(rng_next(&a), rng_next(&b));

    // Zero seed must not get stuck at zero.
    rng_seed(&a, 0);
    CHECK(0 != rng_next(&a));

    // Ranges stay in bounds and hit both ends.
    rng_seed(&a, 42);
    int seen_lo = 0, seen_hi = 0;
    for (int i = 0; i < 10000; i++) {
        int32_t v = rng_between(&a, -3, 3);
        CHECK(v >= -3 && v <= 3);
        seen_lo |= (-3 == v);
        seen_hi |= (3 == v);
    }
    CHECK(seen_lo && seen_hi);
    CHECK_EQ(rng_range(&a, 0), 0);
    CHECK_EQ(rng_between(&a, 5, 5), 5);

    // Hash is deterministic and sensitive to every input.
    CHECK_EQ(rng_hash3(1, 2, 3), rng_hash3(1, 2, 3));
    CHECK(rng_hash3(1, 2, 3) != rng_hash3(1, 2, 4));
    CHECK(rng_hash3(1, 2, 3) != rng_hash3(1, 3, 3));
    CHECK(rng_hash3(1, 2, 3) != rng_hash3(2, 2, 3));
}
