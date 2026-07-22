#include "sender/data_simulator.h"

namespace cmb::sender {

cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns) {
    cmb::proto::Frame frame;
    frame.header.frame_id = frame_id;
    frame.header.timestamp_ns = timestamp_ns;
    frame.payload.resize(cmb::proto::kChannelCount);
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        frame.payload[i] = static_cast<std::uint32_t>(frame_id + i);
    }
    return frame;
}

}  // namespace cmb::sender
