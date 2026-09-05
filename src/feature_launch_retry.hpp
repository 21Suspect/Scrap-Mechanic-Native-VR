#pragma once
#include <atomic>
#include <cstdint>

namespace smvr::features
{
inline bool launch_marker_fresh(uint64_t now, uint64_t modified, uint64_t process_start,
                               uint64_t maximum_age)
{
    const bool launched_promptly = process_start >= modified &&
        process_start - modified <= maximum_age;
    return now >= modified && (now - modified <= maximum_age || launched_promptly);
}

class LaunchRetryWindow
{
public:
    explicit LaunchRetryWindow(uint64_t duration) : duration_(duration) {}
    void request() { pending_.store(true, std::memory_order_release); }
    bool begin_attempt(uint64_t now)
    {
        if (!pending_.exchange(false, std::memory_order_acq_rel)) return false;
        deadline_.store(now + duration_, std::memory_order_release);
        return true;
    }
    uint64_t deadline() const { return deadline_.load(std::memory_order_acquire); }
    bool complete() { return deadline_.exchange(0, std::memory_order_acq_rel) != 0; }
private:
    uint64_t duration_;
    std::atomic<bool> pending_{false};
    std::atomic<uint64_t> deadline_{0};
};
}
