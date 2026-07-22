#pragma once

#include "common/frame.h"

#include <vector>

namespace cmb::sender {

std::vector<std::byte> encode_frame(const cmb::proto::Frame& frame);

}  // namespace cmb::sender
