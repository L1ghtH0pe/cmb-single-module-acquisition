#include "common/frame.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace cmb::proto {
namespace {

template <typename UInt>
void put_le(std::vector<std::byte>& bytes, UInt value) {
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
}

template <typename UInt>
void put_le(std::array<std::byte, kHeaderSize>& bytes, std::size_t& offset, UInt value) {
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        bytes[offset++] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
    }
}

template <typename UInt>
UInt get_le(std::span<const std::byte> bytes, std::size_t& offset) {
    UInt value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        value |= static_cast<UInt>(static_cast<unsigned char>(bytes[offset++])) << (8 * i);
    }
    return value;
}

std::array<std::byte, kHeaderSize> encode_header(FrameHeader header) {
    std::array<std::byte, kHeaderSize> bytes{};
    std::size_t offset = 0;

    put_le(bytes, offset, header.magic);
    put_le(bytes, offset, header.version);
    put_le(bytes, offset, header.header_size);
    put_le(bytes, offset, header.module_id);
    put_le(bytes, offset, header.flags);
    put_le(bytes, offset, header.frame_id);
    put_le(bytes, offset, header.timestamp_ns);
    put_le(bytes, offset, header.channel_count);
    put_le(bytes, offset, header.sample_rate_hz);
    put_le(bytes, offset, header.payload_len);
    put_le(bytes, offset, header.header_crc);

    return bytes;
}

FrameHeader decode_header(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw std::runtime_error("header too short");
    }

    FrameHeader header{};
    std::size_t offset = 0;

    header.magic = get_le<std::uint32_t>(bytes, offset);
    header.version = get_le<std::uint16_t>(bytes, offset);
    header.header_size = get_le<std::uint16_t>(bytes, offset);
    header.module_id = get_le<std::uint16_t>(bytes, offset);
    header.flags = get_le<std::uint16_t>(bytes, offset);
    header.frame_id = get_le<std::uint64_t>(bytes, offset);
    header.timestamp_ns = get_le<std::uint64_t>(bytes, offset);
    header.channel_count = get_le<std::uint16_t>(bytes, offset);
    header.sample_rate_hz = get_le<std::uint16_t>(bytes, offset);
    header.payload_len = get_le<std::uint32_t>(bytes, offset);
    header.header_crc = get_le<std::uint32_t>(bytes, offset);

    return header;
}

}  // namespace

std::vector<std::byte> serialize_frame(const Frame& frame) {
    std::vector<std::byte> result;
    serialize_frame_into(frame, result);
    return result;
}

void serialize_frame_into(const Frame& frame, std::vector<std::byte>& result) {
    FrameHeader header = frame.header;
    header.payload_len = static_cast<std::uint32_t>(frame.payload.size() * sizeof(std::uint32_t));
    header.header_crc = 0;

    auto header_bytes = encode_header(header);
    header.header_crc = crc32(std::span<const std::byte>(header_bytes.data(), header_bytes.size()));
    header_bytes = encode_header(header);

    const std::uint32_t payload_crc_value = crc32(std::span<const std::uint32_t>(frame.payload.data(), frame.payload.size()));

    result.clear();
    result.reserve(kFramePrefixSize + header_bytes.size() + header.payload_len + kFrameCrcSize);

    const std::uint32_t header_len = static_cast<std::uint32_t>(header_bytes.size());
    put_le(result, header_len);
    result.insert(result.end(), header_bytes.begin(), header_bytes.end());

    for (const auto sample : frame.payload) {
        put_le(result, sample);
    }

    put_le(result, payload_crc_value);
}

Frame deserialize_frame(std::span<const std::byte> bytes) {
    Frame frame;
    deserialize_frame_into(bytes, frame);
    return frame;
}

void deserialize_frame_into(std::span<const std::byte> bytes, Frame& frame) {
    if (bytes.size() < kFramePrefixSize + kHeaderSize + kFrameCrcSize) {
        throw std::runtime_error("frame too short");
    }

    std::size_t prefix_offset = 0;
    const std::uint32_t header_len = get_le<std::uint32_t>(bytes, prefix_offset);
    if (header_len != kHeaderSize) {
        throw std::runtime_error("unexpected header length");
    }

    const auto header_span = bytes.subspan(kFramePrefixSize, header_len);
    frame.header = decode_header(header_span);

    if (frame.header.payload_len % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("payload length is not uint32 aligned");
    }
    if (frame.header.payload_len != kPayloadBytes) {
        throw std::runtime_error("unexpected payload length");
    }

    const auto expected_size = kFramePrefixSize + header_len + frame.header.payload_len + kFrameCrcSize;
    if (bytes.size() != expected_size) {
        throw std::runtime_error("frame length mismatch");
    }

    const std::size_t payload_count = frame.header.payload_len / sizeof(std::uint32_t);
    frame.payload.resize(payload_count);

    const auto payload_offset = kFramePrefixSize + header_len;
    std::size_t payload_cursor = payload_offset;
    for (auto& sample : frame.payload) {
        sample = get_le<std::uint32_t>(bytes, payload_cursor);
    }
    frame.payload_crc = get_le<std::uint32_t>(bytes, payload_cursor);
}

std::string validate_frame(const Frame& frame) {
    if (frame.header.magic != kFrameMagic) {
        return "bad magic";
    }
    if (frame.header.version != kProtocolVersion) {
        return "unsupported version";
    }
    if (frame.header.header_size != kHeaderSize) {
        return "bad header size";
    }
    if (frame.header.channel_count != kChannelCount) {
        return "unexpected channel count";
    }
    if (frame.header.sample_rate_hz != kSampleRateHz) {
        return "unexpected sample rate";
    }
    if (frame.header.payload_len != frame.payload.size() * sizeof(std::uint32_t)) {
        return "payload length mismatch";
    }

    auto header_bytes = encode_header(frame.header);
    const auto stored_crc = frame.header.header_crc;
    auto header = frame.header;
    header.header_crc = 0;
    header_bytes = encode_header(header);
    const auto expected_header_crc = crc32(std::span<const std::byte>(header_bytes.data(), header_bytes.size()));
    if (stored_crc != expected_header_crc) {
        return "header crc mismatch";
    }

    const auto expected_payload_crc = crc32(std::span<const std::uint32_t>(frame.payload.data(), frame.payload.size()));
    if (frame.payload_crc != expected_payload_crc) {
        return "payload crc mismatch";
    }

    return {};
}

}  // namespace cmb::proto
