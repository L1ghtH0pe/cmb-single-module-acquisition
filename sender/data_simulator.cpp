#include "sender/data_simulator.h"

namespace cmb::sender {

cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id) {
    cmb::proto::Frame frame;
    frame.payload.resize(cmb::proto::kChannelCount);
    fill_frame(frame, frame_id, timestamp_ns, module_id);
    return frame;
}

void fill_frame(cmb::proto::Frame& frame, std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id) {
    frame.header = {};
    frame.header.module_id = module_id;
    frame.header.frame_id = frame_id;
    frame.header.timestamp_ns = timestamp_ns;
    if (frame.payload.size() != cmb::proto::kChannelCount) {
        frame.payload.resize(cmb::proto::kChannelCount);
    }
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        frame.payload[i] = static_cast<std::uint32_t>(frame_id + i);
    }
}

}  // namespace cmb::sender
