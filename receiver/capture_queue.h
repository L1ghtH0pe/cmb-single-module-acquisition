#pragma once

#include "common/frame.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cmb::receiver {

struct CapturedFrame {
    cmb::proto::Frame frame{};
    std::uint64_t wire_complete_steady_ns{0};
};

class CaptureQueue {
  public:
    explicit CaptureQueue(std::size_t capacity);

    CaptureQueue(const CaptureQueue&) = delete;
    CaptureQueue& operator=(const CaptureQueue&) = delete;

    bool has_space() const;
    CapturedFrame& producer_slot();
    void publish();

    CapturedFrame* consumer_slot();
    void release();

    bool empty() const;
    std::size_t size() const;
    std::size_t capacity() const { return capacity_; }

  private:
    std::size_t next(std::size_t index) const { return (index + 1) % slots_.size(); }

    std::vector<CapturedFrame> slots_;
    std::size_t capacity_{0};
    std::atomic<std::size_t> producer_index_{0};
    std::atomic<std::size_t> consumer_index_{0};
};

}  // namespace cmb::receiver
