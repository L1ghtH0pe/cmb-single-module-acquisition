#include "common/frame.h"

namespace cmb::sender {

cmb::proto::Frame make_frame(std::uint64_t frame_id, std::uint64_t timestamp_ns);

}  // namespace cmb::sender
