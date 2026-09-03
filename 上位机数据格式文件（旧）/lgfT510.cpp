#include "lgfT510.h"
#include <cstdio>

inline bool is_valid_header(const uint64_t *buf, size_t i, size_t total_len) {
  if (i + HEADER_LEN > total_len)
    return false;
  return (buf[i] == STARTWORD2 && buf[i + 1] == STARTWORD1 &&
          (buf[i + 3] == SYNC_WORD || buf[i + 3] == SYNC_WORD_ALT) &&
          buf[i + 5] == SYNC_WORD && buf[i + 6] == STOPWORD2 &&
          buf[i + 7] == STOPWORD1);
}

inline Packet parse_packet(const uint64_t *buf, size_t i, size_t num) {
  Packet pkt{};
  pkt.package_id = buf[i + 2];
  pkt.data_id = buf[i + 4];
  pkt.data_ptr = buf + i + HEADER_LEN;
  pkt.data_len = num;
  return pkt;
}

std::vector<Packet> parse_stream(const uint64_t *uint64_list, size_t total_len,
                                 uint DATA_LEN) {
  std::vector<Packet> packets;
  packets.reserve(total_len / 1024);

  size_t i = 0;
  uint64_t last_id = 0;
  bool has_last = false;

  uint PACKET_LEN = HEADER_LEN + DATA_LEN;
  while (i < total_len - PACKET_LEN) {
    if (is_valid_header(uint64_list, i, total_len)) {
      size_t j = 0;
      const size_t max_j = total_len - PACKET_LEN - i - HEADER_LEN;
      for (; j < max_j; ++j) {
        if (is_valid_header(uint64_list, i + j + HEADER_LEN, total_len)) {
          break;
        }
      }

      if (j >= DATA_LEN) {
        Packet pkt = parse_packet(uint64_list, i, j);
        packets.push_back(pkt);

        if (has_last && pkt.package_id != last_id + 1) {
          printf("[丢包] expected %llu, got %llu\n",
                 (unsigned long long)(last_id + 1),
                 (unsigned long long)pkt.package_id);
        }
        last_id = pkt.package_id;
        has_last = true;
        i += HEADER_LEN + j;
      } else {
        i += HEADER_LEN + j;
      }
    } else {
      ++i;
    }
  }
  return packets;
}
