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
               "send_deadline_us,send_deadline_miss_count,send_max_late_us,send_schedule_late_avg_us,"
               "send_schedule_late_max_us,send_work_avg_us,send_work_max_us,send_encode_avg_us,"
               "send_encode_max_us,send_socket_avg_us,send_socket_max_us,"
               "recv_gap_avg_us,recv_gap_max_us,recv_gap_p999_us,recv_deadline_us,recv_deadline_miss_count,"
               "recv_max_late_us,recv_parse_avg_us,recv_parse_max_us,capture_queue_capacity,"
               "capture_queue_high_water,capture_queue_overrun_count,storage_write_avg_us,storage_write_max_us,"
               "cpu_percent,rss_mb\n";
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
            << snapshot.send_deadline_us << ','
            << snapshot.send_deadline_miss_count << ','
            << snapshot.send_max_late_us << ','
            << snapshot.send_schedule_late_avg_us << ','
            << snapshot.send_schedule_late_max_us << ','
            << snapshot.send_work_avg_us << ','
            << snapshot.send_work_max_us << ','
            << snapshot.send_encode_avg_us << ','
            << snapshot.send_encode_max_us << ','
            << snapshot.send_socket_avg_us << ','
            << snapshot.send_socket_max_us << ','
            << snapshot.recv_gap_avg_us << ','
            << snapshot.recv_gap_max_us << ','
            << snapshot.recv_gap_p999_us << ','
            << snapshot.recv_deadline_us << ','
            << snapshot.recv_deadline_miss_count << ','
            << snapshot.recv_max_late_us << ','
            << snapshot.recv_parse_avg_us << ','
            << snapshot.recv_parse_max_us << ','
            << snapshot.capture_queue_capacity << ','
            << snapshot.capture_queue_high_water << ','
            << snapshot.capture_queue_overrun_count << ','
            << snapshot.storage_write_avg_us << ','
            << snapshot.storage_write_max_us << ','
            << snapshot.cpu_percent << ','
            << snapshot.rss_mb << '\n';
    stream_.flush();
}

}  // namespace cmb::common
