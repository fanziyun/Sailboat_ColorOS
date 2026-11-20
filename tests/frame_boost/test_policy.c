#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "sailboat_coloros_fbg_policy.h"

int main(void)
{
    unsigned long util;
    unsigned int last = 0;

    assert(sailboat_coloros_fbg_freq_floor(512, 1024, 3200000, 300000, 3200000) == 2000000);
    assert(sailboat_coloros_fbg_freq_floor(1024, 1024, 3200000, 300000, 2100000) == 2100000);
    assert(sailboat_coloros_fbg_freq_floor(ULONG_MAX, 1024, 3200000, 300000, 3200000) == 3200000);
    assert(sailboat_coloros_fbg_freq_floor(10, 0, 3200000, 300000, 3200000) == 0);
    assert(sailboat_coloros_fbg_freq_floor(10, 1024, 0, 300000, 3200000) == 0);
    assert(sailboat_coloros_fbg_freq_floor(10, 1024, 3200000, 400000, 300000) == 0);
    assert(sailboat_coloros_fbg_freq_floor(0, 1024, 3200000, 300000, 3200000) == 300000);
    for (util = 0; util < 4096; util++) {
        unsigned int floor = sailboat_coloros_fbg_freq_floor(util, 1024, 3200000, 300000, 2100000);
        assert(floor >= last && floor >= 300000 && floor <= 2100000);
        last = floor;
    }
    puts("FrameBoost frequency floor: proportional, monotonic, saturated and thermal-bounded");
    return 0;
}
