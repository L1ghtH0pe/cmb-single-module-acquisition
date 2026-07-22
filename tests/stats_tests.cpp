#include "common/frame.h"
#include "common/latency_stats.h"
#include "receiver/loss_detector.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

void test_latency_stats() {
    cmb::common::LatencyStats stats;
    stats.add(10);
    stats.add(20);
    stats.add(30);
    assert(stats.count() == 3);
    assert(stats.average() == 20);
    assert(stats.max() == 30);
    assert(stats.percentile(0.999) == 30);
}

void test_loss_detector() {
    cmb::receiver::LossDetector detector;
    detector.observe(10);
    detector.observe(11);
    detector.observe(13);
    detector.observe(13);
    detector.observe(12);

    const auto& stats = detector.stats();
    assert(stats.frame_count == 5);
    assert(stats.first_frame_id == 10);
    assert(stats.last_frame_id == 12);
    assert(stats.gap_count >= 1);
    assert(stats.duplicate_count == 1);
    assert(stats.reorder_count >= 1);
    assert(stats.missing_frame_count >= 1);
}

}  // namespace

int main() {
    test_latency_stats();
    test_loss_detector();
    std::cout << "stats_tests passed\n";
    return 0;
}
