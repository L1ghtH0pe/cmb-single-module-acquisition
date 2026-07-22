#include "receiver/frame_parser.h"

#include <cstring>
#include <stdexcept>

namespace cmb::receiver {

std::optional<cmb::proto::Frame> parse_frame(const std::vector<std::byte>& bytes) {
    try {
        auto frame = cmb::proto::deserialize_frame(bytes);
        if (!cmb::proto::validate_frame(frame).empty()) {
            return std::nullopt;
        }
        return frame;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<cmb::proto::Frame> parse_prefixed_frame(const std::vector<std::byte>& header_and_payload) {
    return parse_frame(header_and_payload);
}

std::uint32_t peek_header_len(const std::vector<std::byte>& prefix) {
    if (prefix.size() != sizeof(std::uint32_t)) {
        throw std::runtime_error("header length prefix must be 4 bytes");
    }
    std::uint32_t header_len = 0;
    std::memcpy(&header_len, prefix.data(), sizeof(header_len));
    if (header_len != cmb::proto::kHeaderSize) {
        throw std::runtime_error("unexpected header length");
    }
    return header_len;
}

std::uint32_t peek_payload_len(const std::vector<std::byte>& header_bytes) {
    if (header_bytes.size() != cmb::proto::kHeaderSize) {
        throw std::runtime_error("header must be 40 bytes");
    }

    constexpr std::size_t payload_len_offset =
        sizeof(std::uint32_t) +     // magic
        sizeof(std::uint16_t) +     // version
        sizeof(std::uint16_t) +     // header_size
        sizeof(std::uint16_t) +     // module_id
        sizeof(std::uint16_t) +     // flags
        sizeof(std::uint64_t) +     // frame_id
        sizeof(std::uint64_t) +     // timestamp_ns
        sizeof(std::uint16_t) +     // channel_count
        sizeof(std::uint16_t);      // sample_rate_hz

    std::uint32_t payload_len = 0;
    std::memcpy(&payload_len, header_bytes.data() + payload_len_offset, sizeof(payload_len));
    return payload_len;
}

}  // namespace cmb::receiver
