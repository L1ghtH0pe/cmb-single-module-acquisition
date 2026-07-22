#include "common/metrics.h"

#include <stdexcept>

namespace cmb::common {

MetricsWriter::MetricsWriter(const std::filesystem::path& path) : stream_(path, std::ios::app) {
    if (!stream_) {
        throw std::runtime_error("failed to open metrics file");
    }
}

void MetricsWriter::write_header() {
    std::lock_guard lock(mutex_);
    stream_ << "timestamp,frame_id_begin,frame_id_end,frame_count,parse_fail_count,crc_error_count,"
               "tcp_disconnect_count,reconnect_ms,send_period_avg_us,send_period_max_us,send_period_p999_us,"
               "recv_gap_avg_us,recv_gap_max_us,recv_gap_p999_us,cpu_percent,rss_mb\n";
    stream_.flush();
}

void MetricsWriter::append(const std::string& timestamp, const MetricsSnapshot& snapshot) {
    std::lock_guard lock(mutex_);
    stream_ << timestamp << ','
            << snapshot.frame_id_begin << ','
            << snapshot.frame_id_end << ','
            << snapshot.frame_count << ','
            << snapshot.parse_fail_count << ','
            << snapshot.crc_error_count << ','
            << snapshot.tcp_disconnect_count << ','
            << snapshot.reconnect_ms << ','
            << snapshot.send_period_avg_us << ','
            << snapshot.send_period_max_us << ','
            << snapshot.send_period_p999_us << ','
            << snapshot.recv_gap_avg_us << ','
            << snapshot.recv_gap_max_us << ','
            << snapshot.recv_gap_p999_us << ','
            << snapshot.cpu_percent << ','
            << snapshot.rss_mb << '\n';
    stream_.flush();
}

}  // namespace cmb::common
