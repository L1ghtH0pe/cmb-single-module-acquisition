#ifndef LGF_T510_H
#define LGF_T510_H

#include <complex>
#include <cstdint>
#include <vector>

constexpr int HEADER_LEN = 8;
constexpr int ZHEN_LEN = (512 * 1024) / 8;

constexpr uint64_t STARTWORD1 = 0xFFEEDDCCBBAA9988;
constexpr uint64_t STARTWORD2 = 0x7766554433221100;
constexpr uint64_t SYNC_WORD = 0xFFFFFFFFFFFFFFFF;
constexpr uint64_t SYNC_WORD_ALT = 0xFFFFFFFF00000000;
constexpr uint64_t STOPWORD1 = 0x0011223344556677;
constexpr uint64_t STOPWORD2 = 0x8899AABBCCDDEEFF;

struct Packet {
  uint64_t package_id;
  uint64_t data_id;
  const uint64_t *data_ptr;
  size_t data_len;
};

static inline int32_t convert_uint32_to_int32(uint32_t x) {
  if (x & 0x80000000) {
    return -(0x100000000LL - (int64_t)x);
  }
  return (int32_t)x;
}

bool is_valid_header(const uint64_t *buf, size_t i, size_t total_len);
Packet parse_packet(const uint64_t *buf, size_t i, size_t num);
std::vector<Packet> parse_stream(const uint64_t *uint64_list, size_t total_len,
                                 uint DATA_LEN);

#endif
