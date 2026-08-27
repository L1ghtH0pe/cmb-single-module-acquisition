#pragma once

#include "common/frame.h"

#include <vector>

namespace cmb::sender {

std::vector<std::byte> encode_frame(const cmb::proto::Frame& frame);
void encode_frame_into(const cmb::proto::Frame& frame, std::vector<std::byte>& out);

}  // namespace cmb::sender
