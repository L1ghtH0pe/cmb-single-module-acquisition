#pragma once

#include "common/frame.h"

#include <cstddef>

namespace cmb::sender {

cmb::proto::ComplexSample simulated_sample(std::uint64_t frame_id, std::size_t channel);
cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id = 0);
void fill_frame(cmb::proto::Frame& frame, std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id = 0);

}  // namespace cmb::sender
