#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cmb::proto {

inline constexpr std::uint32_t kFrameMagic = 0x434D4231;  // CMB1
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kChannelCount = 1704;
inline constexpr std::uint16_t kSampleRateHz = 200;
inline constexpr std::size_t kPayloadBytes = kChannelCount * sizeof(std::uint32_t);
inline constexpr std::size_t kHeaderSize = 40;
inline constexpr std::size_t kFramePrefixSize = sizeof(std::uint32_t);
inline constexpr std::size_t kFrameCrcSize = sizeof(std::uint32_t);
inline constexpr std::size_t kWireFrameSize = kFramePrefixSize + kHeaderSize + kPayloadBytes + kFrameCrcSize;

struct FrameHeader {
    std::uint32_t magic{kFrameMagic};
    std::uint16_t version{kProtocolVersion};
    std::uint16_t header_size{kHeaderSize};
    std::uint16_t module_id{0};
    std::uint16_t flags{0};
    std::uint64_t frame_id{0};
    std::uint64_t timestamp_ns{0};
    std::uint16_t channel_count{kChannelCount};
    std::uint16_t sample_rate_hz{kSampleRateHz};
    std::uint32_t payload_len{static_cast<std::uint32_t>(kPayloadBytes)};
    std::uint32_t header_crc{0};
};

struct Frame {
    FrameHeader header{};
    std::vector<std::uint32_t> payload{};
    std::uint32_t payload_crc{0};
};

std::uint32_t crc32(std::span<const std::byte> bytes);
std::uint32_t crc32(std::span<const std::uint32_t> values);

std::vector<std::byte> serialize_frame(const Frame& frame);
void serialize_frame_into(const Frame& frame, std::vector<std::byte>& out);
Frame deserialize_frame(std::span<const std::byte> bytes);
void deserialize_frame_into(std::span<const std::byte> bytes, Frame& out);
std::string validate_frame(const Frame& frame);

}  // namespace cmb::proto
