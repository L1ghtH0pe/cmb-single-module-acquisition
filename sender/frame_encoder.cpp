#include "sender/frame_encoder.h"

namespace cmb::sender {

std::vector<std::byte> encode_frame(const cmb::proto::Frame& frame) {
    return cmb::proto::serialize_frame(frame);
}

}  // namespace cmb::sender
