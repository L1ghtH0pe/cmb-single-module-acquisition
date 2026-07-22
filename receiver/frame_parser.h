#pragma once

#include "common/frame.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cmb::receiver {

enum class ParseError {
    kNone,
    kMalformed,
    kHeaderCrc,
    kPayloadCrc,
    kUnexpectedProtocol,
};

struct ParseResult {
    std::optional<cmb::proto::Frame> frame{};
    ParseError error{ParseError::kNone};
    std::string message{};

    bool ok() const { return frame.has_value() && error == ParseError::kNone; }
};

ParseResult parse_frame(const std::vector<std::byte>& bytes);
ParseResult parse_prefixed_frame(const std::vector<std::byte>& header_and_payload);
std::uint32_t peek_header_len(const std::vector<std::byte>& prefix);
std::uint32_t peek_payload_len(const std::vector<std::byte>& header_bytes);

}  // namespace cmb::receiver
