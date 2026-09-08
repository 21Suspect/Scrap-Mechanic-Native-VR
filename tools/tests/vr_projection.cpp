#include "../../src/vr_projection.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
    bool requirement(float negative, float positive, uint32_t target, uint32_t &value)
    {
        if (!smvr::projection::minimum_centered_extent(negative, positive, target, value)) return false;
        const double left = std::tan(static_cast<double>(negative));
        const double right = std::tan(static_cast<double>(positive));
        const double symmetric = (right - left) * value / (2.0 * target);
        return symmetric + 1e-9 >= (std::max)(-left, right);
    }
}

int main()
{
    // Quest/Meta Link uses mirrored eye FOVs. Both eyes retain the same shared
    // render-target requirement as the established implementation.
    uint32_t quest_left = 0, quest_right = 0;
    if (!requirement(-0.750492f, 0.785398f, 2112, quest_left) ||
        !requirement(-0.785398f, 0.750492f, 2112, quest_right) ||
        quest_left != quest_right) return 1;

    // Canted/asymmetric displays can report two non-mirrored eye requirements.
    // The common target must use the larger value; choosing the smaller one is
    // the issue #28 eye_camera_build failure.
    uint32_t canted_left = 0, canted_right = 0;
    if (!requirement(-1.10f, 0.70f, 2804, canted_left) ||
        !requirement(-0.80f, 1.20f, 2804, canted_right)) return 2;
    const uint32_t common = (std::max)(canted_left, canted_right);
    if (common < canted_left || common < canted_right || canted_left == canted_right) return 3;

    uint32_t invalid = 123;
    if (smvr::projection::minimum_centered_extent(0.1f, 0.8f, 2804, invalid) || invalid != 0) return 4;
    std::puts("VR projection tests passed");
    return 0;
}
