#include "receiver/frame_parser.h"

#include <cstring>
#include <stdexcept>

namespace cmb::receiver {
namespace {

std::uint32_t read_u32_le(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }
    return value;
}

ParseError classify_validation_error(const std::string& error) {
    if (error.empty()) {
        return ParseError::kNone;
    }
    if (error == "header crc mismatch") {
        return ParseError::kHeaderCrc;
    }
    if (error == "payload crc mismatch") {
        return ParseError::kPayloadCrc;
    }
    return ParseError::kUnexpectedProtocol;
}

}  // namespace

ParseResult parse_frame(const std::vector<std::byte>& bytes) {
    try {
        auto frame = cmb::proto::deserialize_frame(bytes);
        const auto validation_error = cmb::proto::validate_frame(frame);
        const auto parse_error = classify_validation_error(validation_error);
        if (parse_error != ParseError::kNone) {
            return {.frame = std::nullopt, .error = parse_error, .message = validation_error};
        }
        return {.frame = frame, .error = ParseError::kNone, .message = {}};
    } catch (const std::exception& ex) {
        return {.frame = std::nullopt, .error = ParseError::kMalformed, .message = ex.what()};
    } catch (...) {
        return {.frame = std::nullopt, .error = ParseError::kMalformed, .message = "unknown parse failure"};
    }
}

ParseResult parse_prefixed_frame(const std::vector<std::byte>& header_and_payload) {
    return parse_frame(header_and_payload);
}

std::uint32_t peek_header_len(const std::vector<std::byte>& prefix) {
    if (prefix.size() != sizeof(std::uint32_t)) {
        throw std::runtime_error("header length prefix must be 4 bytes");
    }
    const std::uint32_t header_len = read_u32_le(prefix, 0);
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

    return read_u32_le(header_bytes, payload_len_offset);
}

}  // namespace cmb::receiver
