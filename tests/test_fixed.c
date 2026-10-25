// Fixed-point helpers.

#include "test.h"
#include "fixed.h"

void run_fixed_tests(void)
{
    CHECK_EQ(fx_mul(FX(2.0), FX(3.5)), FX(7.0));
    CHECK_EQ(fx_mul(FX(-2.0), FX(0.5)), FX(-1.0));
    CHECK_EQ(fx_div(FX(7.0), FX(2.0)), FX(3.5));
    CHECK_EQ(fx_sqrt(FX(4.0)), FX(2.0));
    CHECK_EQ(fx_sqrt(FX(144.0)), FX(12.0));
    CHECK(fx_abs(fx_sqrt(FX(2.0)) - FX(1.41421)) <= 1);
    CHECK_EQ(fx_sqrt(0), 0);
    CHECK_EQ(fx_sqrt(-5), 0);
    CHECK_EQ(fx_lerp(FX(1.0), FX(3.0), FX_HALF), FX(2.0));
    CHECK_EQ(fx_smooth(0), 0);
    CHECK_EQ(fx_smooth(FX_ONE), FX_ONE);
    CHECK_EQ(fx_smooth(FX_HALF), FX_HALF);
    CHECK_EQ(fx_clamp(FX(5.0), 0, FX(2.0)), FX(2.0));
}
