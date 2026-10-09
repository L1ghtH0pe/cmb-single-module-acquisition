#include "common/frame.h"

namespace cmb::proto {
namespace {
constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320u;
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= static_cast<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(-(crc & 1u));
            crc = (crc >> 1u) ^ (kCrc32Polynomial & mask);
        }
    }
    return ~crc;
}

}  // namespace cmb::proto
