#pragma once

namespace smvr::features
{
class UiTriggerGate
{
public:
    bool gameplay_down(bool ui_visible, bool down, bool released)
    {
        if (ui_visible) release_required_ = true;
        else if (released) release_required_ = false;
        return down && !release_required_;
    }
private:
    bool release_required_ = false;
};
}
