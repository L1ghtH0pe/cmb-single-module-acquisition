#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace cmb::common {

struct MetricsSnapshot {
    std::uint64_t frame_id_begin{0};
    std::uint64_t frame_id_end{0};
    std::uint64_t frame_count{0};
    std::uint64_t parse_fail_count{0};
    std::uint64_t crc_error_count{0};
    std::uint64_t tcp_disconnect_count{0};
    std::uint64_t reconnect_ms{0};
    std::uint64_t send_period_avg_us{0};
    std::uint64_t send_period_max_us{0};
    std::uint64_t send_period_p999_us{0};
    std::uint64_t recv_gap_avg_us{0};
    std::uint64_t recv_gap_max_us{0};
    std::uint64_t recv_gap_p999_us{0};
    double cpu_percent{0.0};
    std::uint64_t rss_mb{0};
};

class MetricsWriter {
  public:
    explicit MetricsWriter(const std::filesystem::path& path);

    void write_header();
    void append(const std::string& timestamp, const MetricsSnapshot& snapshot);

  private:
    std::ofstream stream_;
    std::mutex mutex_;
};

}  // namespace cmb::common
