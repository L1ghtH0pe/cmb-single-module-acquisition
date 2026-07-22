#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cmb::common {

class LatencyStats {
  public:
    void add(std::uint64_t value_us);
    bool empty() const { return samples_.empty(); }
    std::uint64_t count() const { return static_cast<std::uint64_t>(samples_.size()); }
    std::uint64_t max() const;
    std::uint64_t average() const;
    std::uint64_t percentile(double p) const;

  private:
    std::vector<std::uint64_t> samples_;
};

}  // namespace cmb::common
