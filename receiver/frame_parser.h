#pragma once

#include "common/frame.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace cmb::receiver {

std::optional<cmb::proto::Frame> parse_frame(const std::vector<std::byte>& bytes);
std::optional<cmb::proto::Frame> parse_prefixed_frame(const std::vector<std::byte>& header_and_payload);
std::uint32_t peek_header_len(const std::vector<std::byte>& prefix);
std::uint32_t peek_payload_len(const std::vector<std::byte>& header_bytes);

}  // namespace cmb::receiver
