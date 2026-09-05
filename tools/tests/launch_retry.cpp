#include "../../src/feature_launch_retry.hpp"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace smvr::features;
    assert(launch_marker_fresh(120, 100, 110, 300));
    assert(launch_marker_fresh(1000, 100, 110, 300)); // long cold loading
    assert(!launch_marker_fresh(1000, 100, 900, 300)); // stale, unrelated launch
    assert(!launch_marker_fresh(1000, 100, 0, 300)); // unavailable process time
    assert(!launch_marker_fresh(90, 100, 110, 300)); // future marker
    LaunchRetryWindow ordinary(180000);
    assert(!ordinary.begin_attempt(10000) && ordinary.deadline() == 0);
    LaunchRetryWindow requested(180000);
    requested.request();
    assert(requested.deadline() == 0); // loading has not started the budget
    assert(requested.begin_attempt(600000)); // ten-minute cold load
    assert(requested.deadline() == 780000);
    assert(!requested.begin_attempt(605000)); // retries do not extend it
    assert(requested.deadline() == 780000);
    assert(!requested.begin_attempt(800000)); // expired budget stays expired
    assert(requested.deadline() == 780000);
    assert(requested.complete()); // only a running session completes handoff
    assert(requested.deadline() == 0 && !requested.complete());
    std::puts("PASS: cold-loading budget, stale markers, desktop launch, bounded retries, session completion");
}
