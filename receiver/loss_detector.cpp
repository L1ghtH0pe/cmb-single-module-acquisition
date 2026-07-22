#include "receiver/loss_detector.h"

namespace cmb::receiver {

void LossDetector::observe(std::uint64_t frame_id) {
    if (!seen_any_) {
        seen_any_ = true;
        stats_.first_frame_id = frame_id;
        stats_.last_frame_id = frame_id;
        stats_.frame_count = 1;
        return;
    }

    ++stats_.frame_count;
    if (frame_id == stats_.last_frame_id + 1) {
        stats_.last_frame_id = frame_id;
        return;
    }

    if (frame_id == stats_.last_frame_id) {
        ++stats_.duplicate_count;
        return;
    }

    if (frame_id > stats_.last_frame_id + 1) {
        ++stats_.gap_count;
        stats_.missing_frame_count += frame_id - stats_.last_frame_id - 1;
        stats_.last_frame_id = frame_id;
        return;
    }

    ++stats_.reorder_count;
    stats_.last_frame_id = frame_id;
}

}  // namespace cmb::receiver
