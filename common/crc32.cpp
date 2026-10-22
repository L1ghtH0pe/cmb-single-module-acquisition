#include "common/frame.h"

#include <array>

namespace cmb::proto {
namespace {

constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320u;

// Slice-by-8 tables for the reflected CRC-32 (same result as the bitwise form and zlib.crc32).
using Tables = std::array<std::array<std::uint32_t, 256>, 8>;

constexpr Tables make_tables() {
    Tables t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? kCrc32Polynomial : 0u);
        }
        t[0][i] = crc;
    }
    for (std::uint32_t i = 0; i < 256; ++i) {
        for (std::size_t k = 1; k < 8; ++k) {
            t[k][i] = (t[k - 1][i] >> 8u) ^ t[0][t[k - 1][i] & 0xFFu];
        }
    }
    return t;
}

constexpr Tables kTables = make_tables();

}  // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t n = bytes.size();
    while (n >= 8) {
        const std::uint32_t lo = crc ^ (static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8u |
                                        static_cast<std::uint32_t>(p[2]) << 16u | static_cast<std::uint32_t>(p[3]) << 24u);
        crc = kTables[7][lo & 0xFFu] ^ kTables[6][(lo >> 8u) & 0xFFu] ^ kTables[5][(lo >> 16u) & 0xFFu] ^
              kTables[4][lo >> 24u] ^ kTables[3][p[4]] ^ kTables[2][p[5]] ^ kTables[1][p[6]] ^ kTables[0][p[7]];
        p += 8;
        n -= 8;
    }
    while (n-- > 0) {
        crc = (crc >> 8u) ^ kTables[0][(crc ^ *p++) & 0xFFu];
    }
    return ~crc;
}

}  // namespace cmb::proto
