#include "receiver/capture_queue.h"

#include <stdexcept>

namespace cmb::receiver {

CaptureQueue::CaptureQueue(std::size_t capacity) : slots_(capacity + 1), capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("capture queue capacity must be positive");
    }
    for (auto& slot : slots_) {
        slot.frame.payload.resize(cmb::proto::kChannelCount);
    }
}

bool CaptureQueue::has_space() const {
    const auto producer = producer_index_.load(std::memory_order_relaxed);
    const auto consumer = consumer_index_.load(std::memory_order_acquire);
    return next(producer) != consumer;
}

CapturedFrame& CaptureQueue::producer_slot() {
    return slots_[producer_index_.load(std::memory_order_relaxed)];
}

void CaptureQueue::publish() {
    const auto producer = producer_index_.load(std::memory_order_relaxed);
    producer_index_.store(next(producer), std::memory_order_release);
}

CapturedFrame* CaptureQueue::consumer_slot() {
    const auto consumer = consumer_index_.load(std::memory_order_relaxed);
    const auto producer = producer_index_.load(std::memory_order_acquire);
    return consumer == producer ? nullptr : &slots_[consumer];
}

void CaptureQueue::release() {
    const auto consumer = consumer_index_.load(std::memory_order_relaxed);
    consumer_index_.store(next(consumer), std::memory_order_release);
}

bool CaptureQueue::empty() const {
    return consumer_index_.load(std::memory_order_acquire) == producer_index_.load(std::memory_order_acquire);
}

std::size_t CaptureQueue::size() const {
    const auto producer = producer_index_.load(std::memory_order_acquire);
    const auto consumer = consumer_index_.load(std::memory_order_acquire);
    return producer >= consumer ? producer - consumer : slots_.size() - consumer + producer;
}

}  // namespace cmb::receiver
