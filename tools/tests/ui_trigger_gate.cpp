#include "../../src/feature_ui_trigger_gate.hpp"
#include <cassert>
#include <cstdio>

int main()
{
    smvr::features::UiTriggerGate right, left;
    assert(right.gameplay_down(false, true, false)); // ordinary tool press
    assert(!right.gameplay_down(true, true, false)); // select palette color
    assert(!right.gameplay_down(false, true, false)); // palette closes while held
    for (int i = 0; i < 300; ++i)
        assert(!right.gameplay_down(false, true, false)); // no timer-based rearm
    assert(!right.gameplay_down(false, false, false)); // inactive sample isn't release
    assert(!right.gameplay_down(false, true, false));
    assert(!right.gameplay_down(false, false, true)); // physical release / unpinch
    assert(right.gameplay_down(false, true, false)); // next press works
    assert(!left.gameplay_down(true, false, true)); // UI owns both hands
    assert(!left.gameplay_down(false, true, false));
    assert(!left.gameplay_down(false, false, true));
    assert(left.gameplay_down(false, true, false));
    assert(!right.gameplay_down(true, false, true)); // menu closed with no hold
    assert(!right.gameplay_down(false, false, true));
    assert(right.gameplay_down(false, true, false));
    std::puts("PASS: UI trigger release, long holds, tracking gaps, optical release, independent hands");
}
