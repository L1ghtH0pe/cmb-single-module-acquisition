#include "common/frame.h"

namespace cmb::sender {

cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id = 0);
void fill_frame(cmb::proto::Frame& frame, std::uint64_t frame_id, std::uint64_t timestamp_ns, std::uint16_t module_id = 0);

}  // namespace cmb::sender
