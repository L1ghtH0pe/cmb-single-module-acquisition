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

void test_loss_detector_in_order() {
    cmb::receiver::LossDetector detector;
    detector.observe(10);
    detector.observe(11);
    detector.observe(12);

    const auto& stats = detector.stats();
    assert(stats.frame_count == 3);
    assert(stats.first_frame_id == 10);
    assert(stats.last_frame_id == 12);
    assert(stats.gap_count == 0);
    assert(stats.duplicate_count == 0);
    assert(stats.reorder_count == 0);
    assert(stats.missing_frame_count == 0);
}

void test_loss_detector_gap() {
    cmb::receiver::LossDetector detector;
    detector.observe(10);
    detector.observe(13);

    const auto& stats = detector.stats();
    assert(stats.frame_count == 2);
    assert(stats.last_frame_id == 13);
    assert(stats.gap_count == 1);
    assert(stats.missing_frame_count == 2);
}

void test_loss_detector_duplicate_does_not_advance() {
    cmb::receiver::LossDetector detector;
    detector.observe(10);
    detector.observe(10);
    detector.observe(11);

    const auto& stats = detector.stats();
    assert(stats.frame_count == 3);
    assert(stats.last_frame_id == 11);
    assert(stats.duplicate_count == 1);
    assert(stats.gap_count == 0);
}

void test_loss_detector_reorder_does_not_lower_high_water_mark() {
    cmb::receiver::LossDetector detector;
    detector.observe(10);
    detector.observe(12);
    detector.observe(11);
    detector.observe(13);

    const auto& stats = detector.stats();
    assert(stats.frame_count == 4);
    assert(stats.last_frame_id == 13);
    assert(stats.gap_count == 1);
    assert(stats.missing_frame_count == 1);
    assert(stats.reorder_count == 1);
}

}  // namespace

int main() {
    test_latency_stats();
    test_loss_detector_in_order();
    test_loss_detector_gap();
    test_loss_detector_duplicate_does_not_advance();
    test_loss_detector_reorder_does_not_lower_high_water_mark();
    std::cout << "stats_tests passed\n";
    return 0;
}
