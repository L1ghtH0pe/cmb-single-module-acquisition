#include "common/latency_stats.h"

#include <numeric>
#include <stdexcept>

namespace cmb::common {

void LatencyStats::add(std::uint64_t value_us) {
    samples_.push_back(value_us);
}

std::uint64_t LatencyStats::max() const {
    if (samples_.empty()) {
        return 0;
    }
    return *std::max_element(samples_.begin(), samples_.end());
}

std::uint64_t LatencyStats::average() const {
    if (samples_.empty()) {
        return 0;
    }
    const auto total = std::accumulate(samples_.begin(), samples_.end(), std::uint64_t{0});
    return total / samples_.size();
}

std::uint64_t LatencyStats::percentile(double p) const {
    if (samples_.empty()) {
        return 0;
    }
    if (p <= 0.0) {
        return *std::min_element(samples_.begin(), samples_.end());
    }
    if (p >= 1.0) {
        return max();
    }
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    const auto index = static_cast<std::size_t>((sorted.size() - 1) * p + 0.5);
    return sorted[index];
}

}  // namespace cmb::common
