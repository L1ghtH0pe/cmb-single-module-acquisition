#pragma once

#include <cstdint>

namespace cmb::receiver {

struct ContinuityStats {
    std::uint64_t frame_count{0};
    std::uint64_t first_frame_id{0};
    std::uint64_t last_frame_id{0};
    std::uint64_t gap_count{0};
    std::uint64_t duplicate_count{0};
    std::uint64_t reorder_count{0};
    std::uint64_t missing_frame_count{0};
};

class LossDetector {
  public:
    void observe(std::uint64_t frame_id);
    const ContinuityStats& stats() const { return stats_; }

  private:
    ContinuityStats stats_{};
    bool seen_any_{false};
};

}  // namespace cmb::receiver
