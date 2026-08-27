#include "common/latency_stats.h"

#include <algorithm>
#include <limits>

namespace cmb::common {

void LatencyStats::add(std::uint64_t value_us) {
    const auto bucket = std::min<std::uint64_t>(value_us, kExactRangeUs);
    ++buckets_[bucket];
    ++count_;
    total_ += value_us;
    if (count_ == 1) {
        min_ = value_us;
        max_ = value_us;
        return;
    }
    min_ = std::min(min_, value_us);
    max_ = std::max(max_, value_us);
}

std::uint64_t LatencyStats::average() const {
    return count_ == 0 ? 0 : total_ / count_;
}

std::uint64_t LatencyStats::percentile(double p) const {
    if (count_ == 0 || p <= 0.0) {
        return count_ == 0 ? 0 : min_;
    }
    if (p >= 1.0) {
        return max_;
    }

    const auto rank = static_cast<std::uint64_t>((count_ - 1) * p + 0.5);
    std::uint64_t cumulative = 0;
    for (std::size_t bucket = 0; bucket < buckets_.size(); ++bucket) {
        cumulative += buckets_[bucket];
        if (cumulative > rank) {
            return bucket == kExactRangeUs ? max_ : bucket;
        }
    }
    return max_;
}

}  // namespace cmb::common
