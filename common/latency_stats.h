#pragma once

#include <array>
#include <cstdint>

namespace cmb::common {

class LatencyStats {
  public:
    void add(std::uint64_t value_us);
    bool empty() const { return count_ == 0; }
    std::uint64_t count() const { return count_; }
    std::uint64_t max() const { return max_; }
    std::uint64_t average() const;
    std::uint64_t percentile(double p) const;

  private:
    static constexpr std::size_t kExactRangeUs = 20'000;

    std::array<std::uint64_t, kExactRangeUs + 1> buckets_{};
    std::uint64_t count_{0};
    std::uint64_t total_{0};
    std::uint64_t min_{0};
    std::uint64_t max_{0};
};

}  // namespace cmb::common
