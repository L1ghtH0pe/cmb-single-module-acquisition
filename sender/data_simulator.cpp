#include "sender/data_simulator.h"

namespace cmb::sender {

cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id) {
    cmb::proto::Frame frame;
    frame.payload.resize(cmb::proto::kChannelCount);
    fill_frame(frame, frame_id, timestamp_ns, module_id);
    return frame;
}

cmb::proto::ComplexSample simulated_sample(std::uint64_t frame_id, std::size_t channel) {
    // re counts up, im = ~re, so both signs and the two's-complement path are exercised.
    const auto base = static_cast<std::uint32_t>(frame_id + channel);
    return {static_cast<std::int32_t>(base), static_cast<std::int32_t>(~base)};
}

void fill_frame(cmb::proto::Frame& frame, std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id) {
    frame.header = {};
    frame.header.module_id = module_id;
    frame.header.frame_id = frame_id;
    frame.header.timestamp_ns = timestamp_ns;
    if (frame.payload.size() != cmb::proto::kChannelCount) {
        frame.payload.resize(cmb::proto::kChannelCount);
    }
    for (std::size_t channel = 0; channel < frame.payload.size(); ++channel) {
        frame.payload[channel] = simulated_sample(frame_id, channel);
    }
}

}  // namespace cmb::sender
