#include "sender/frame_encoder.h"

namespace cmb::sender {

std::vector<std::byte> encode_frame(const cmb::proto::Frame& frame) {
    return cmb::proto::serialize_frame(frame);
}

void encode_frame_into(const cmb::proto::Frame& frame, std::vector<std::byte>& out) {
    cmb::proto::serialize_frame_into(frame, out);
}

}  // namespace cmb::sender
