#include "common/frame.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace cmb::proto {
namespace {

std::array<std::byte, kHeaderSize> encode_header(FrameHeader header) {
    std::array<std::byte, kHeaderSize> bytes{};
    std::size_t offset = 0;

    auto put = [&](auto value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
        offset += sizeof(value);
    };

    put(header.magic);
    put(header.version);
    put(header.header_size);
    put(header.module_id);
    put(header.flags);
    put(header.frame_id);
    put(header.timestamp_ns);
    put(header.channel_count);
    put(header.sample_rate_hz);
    put(header.payload_len);
    put(header.header_crc);

    return bytes;
}

FrameHeader decode_header(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw std::runtime_error("header too short");
    }

    FrameHeader header{};
    std::size_t offset = 0;

    auto get = [&](auto& value) {
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        offset += sizeof(value);
    };

    get(header.magic);
    get(header.version);
    get(header.header_size);
    get(header.module_id);
    get(header.flags);
    get(header.frame_id);
    get(header.timestamp_ns);
    get(header.channel_count);
    get(header.sample_rate_hz);
    get(header.payload_len);
    get(header.header_crc);

    return header;
}

}  // namespace

std::vector<std::byte> serialize_frame(const Frame& frame) {
    FrameHeader header = frame.header;
    header.payload_len = static_cast<std::uint32_t>(frame.payload.size() * sizeof(std::uint32_t));
    header.header_crc = 0;

    auto header_bytes = encode_header(header);
    header.header_crc = crc32(std::span<const std::byte>(header_bytes.data(), header_bytes.size()));
    header_bytes = encode_header(header);

    const std::uint32_t payload_crc_value = crc32(std::span<const std::uint32_t>(frame.payload.data(), frame.payload.size()));

    std::vector<std::byte> result;
    result.reserve(sizeof(std::uint32_t) + header_bytes.size() + header.payload_len + sizeof(std::uint32_t));

    const std::uint32_t header_len = static_cast<std::uint32_t>(header_bytes.size());
    const auto* header_len_bytes = reinterpret_cast<const std::byte*>(&header_len);
    result.insert(result.end(), header_len_bytes, header_len_bytes + sizeof(header_len));
    result.insert(result.end(), header_bytes.begin(), header_bytes.end());

    const auto* payload_bytes = reinterpret_cast<const std::byte*>(frame.payload.data());
    result.insert(result.end(), payload_bytes, payload_bytes + header.payload_len);

    const auto* payload_crc_bytes = reinterpret_cast<const std::byte*>(&payload_crc_value);
    result.insert(result.end(), payload_crc_bytes, payload_crc_bytes + sizeof(payload_crc_value));

    return result;
}

Frame deserialize_frame(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(std::uint32_t) + kHeaderSize + sizeof(std::uint32_t)) {
        throw std::runtime_error("frame too short");
    }

    std::uint32_t header_len = 0;
    std::memcpy(&header_len, bytes.data(), sizeof(header_len));
    if (header_len != kHeaderSize) {
        throw std::runtime_error("unexpected header length");
    }

    const auto header_span = bytes.subspan(sizeof(header_len), header_len);
    Frame frame;
    frame.header = decode_header(header_span);

    if (frame.header.payload_len % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("payload length is not uint32 aligned");
    }
    if (frame.header.payload_len != kPayloadBytes) {
        throw std::runtime_error("unexpected payload length");
    }

    const auto expected_size = sizeof(header_len) + header_len + frame.header.payload_len + sizeof(std::uint32_t);
    if (bytes.size() != expected_size) {
        throw std::runtime_error("frame length mismatch");
    }

    const std::size_t payload_count = frame.header.payload_len / sizeof(std::uint32_t);
    frame.payload.resize(payload_count);

    const auto payload_offset = sizeof(header_len) + header_len;
    std::memcpy(frame.payload.data(), bytes.data() + payload_offset, frame.header.payload_len);
    std::memcpy(&frame.payload_crc, bytes.data() + payload_offset + frame.header.payload_len, sizeof(frame.payload_crc));

    return frame;
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
