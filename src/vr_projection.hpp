#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace smvr::projection
{
    // Return the smallest centered render extent whose sub-frustum can contain
    // an asymmetric runtime eye FOV at the runtime's requested pixel density.
    // A stereo pair must use the maximum requirement from both eyes because
    // Scrap Mechanic renders both eyes through one shared-size target.
    inline bool minimum_centered_extent(float negative_angle, float positive_angle,
                                        uint32_t target_extent, uint32_t &source_extent)
    {
        source_extent = 0;
        if (target_extent < 16) return false;
        const double negative = std::tan(static_cast<double>(negative_angle));
        const double positive = std::tan(static_cast<double>(positive_angle));
        if (!std::isfinite(negative) || !std::isfinite(positive) ||
            negative >= -0.001 || positive <= 0.001) return false;
        const double span = positive - negative;
        const double symmetric = (std::max)(-negative, positive);
        const double required = 2.0 * static_cast<double>(target_extent) * symmetric / span;
        if (!std::isfinite(required) || required < 1.0 ||
            required > static_cast<double>((std::numeric_limits<uint32_t>::max)())) return false;
        source_extent = (std::max)(target_extent, static_cast<uint32_t>(std::ceil(required)));
        return true;
    }
}
