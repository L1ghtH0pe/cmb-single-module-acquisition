#include "common/frame.h"
#include "common/logger.h"
#include "common/latency_stats.h"
#include "common/metrics.h"
#include "receiver/frame_parser.h"
#include "receiver/loss_detector.h"
#include "receiver/storage_writer.h"
#include "receiver/tcp_receiver.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint64_t micros_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
}

std::uint16_t parse_port(const char* text) {
    const long value = std::stol(text);
    if (value <= 0 || value > 65535) {
        throw std::out_of_range("port must be in 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

std::uint64_t parse_frame_count(const char* text) {
    const unsigned long long value = std::stoull(text);
    if (value == 0) {
        throw std::out_of_range("expected_frame_count must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

void print_usage() {
    std::cerr << "usage: receiver [port] [expected_frame_count]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 9000;
    std::uint64_t expected_frames = 10;
    try {
        port = argc > 1 ? parse_port(argv[1]) : port;
        expected_frames = argc > 2 ? parse_frame_count(argv[2]) : expected_frames;
    } catch (const std::exception& ex) {
        print_usage();
        std::cerr << "receiver argument error: " << ex.what() << "\n";
        return 64;
    }

    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("captures/raw");
    cmb::common::Logger logger{"logs/receiver-runtime.log"};
    cmb::common::MetricsWriter metrics{"logs/receiver-metrics.csv"};
    metrics.write_header();

    cmb::receiver::TcpReceiver receiver;
    if (!receiver.listen_on(port)) {
        logger.log(cmb::common::LogLevel::kError, "failed to listen on port " + std::to_string(port));
        return 1;
    }

    logger.log(cmb::common::LogLevel::kInfo, "listening on port " + std::to_string(port));
    std::cout << "receiver listening on port " << port << "\n";

    if (!receiver.accept_one()) {
        logger.log(cmb::common::LogLevel::kError, "failed to accept client");
        return 2;
    }

    logger.log(cmb::common::LogLevel::kInfo, "client accepted");

    cmb::common::MetricsSnapshot snapshot;
    cmb::receiver::LossDetector continuity;
    cmb::common::LatencyStats recv_gap_stats;
    std::chrono::steady_clock::time_point previous_receive_time{};
    bool has_previous_receive_time{false};

    cmb::receiver::StorageWriter storage{"captures/raw"};
    for (std::uint64_t i = 0; i < expected_frames; ++i) {
        std::vector<std::byte> prefix;
        if (!receiver.receive_exact(prefix, sizeof(std::uint32_t))) {
            logger.log(cmb::common::LogLevel::kError, "failed to read header length");
            return 3;
        }

        const auto header_len = cmb::receiver::peek_header_len(prefix);
        std::vector<std::byte> header_bytes;
        if (!receiver.receive_exact(header_bytes, header_len)) {
            logger.log(cmb::common::LogLevel::kError, "failed to read header");
            return 4;
        }

        const auto payload_len = cmb::receiver::peek_payload_len(header_bytes);
        if (payload_len != cmb::proto::kPayloadBytes) {
            ++snapshot.parse_fail_count;
            logger.log(cmb::common::LogLevel::kError, "unexpected payload length " + std::to_string(payload_len));
            return 5;
        }

        std::vector<std::byte> payload_and_crc;
        if (!receiver.receive_exact(payload_and_crc, payload_len + sizeof(std::uint32_t))) {
            logger.log(cmb::common::LogLevel::kError, "failed to read payload");
            return 5;
        }

        std::vector<std::byte> frame_bytes;
        frame_bytes.reserve(prefix.size() + header_bytes.size() + payload_and_crc.size());
        frame_bytes.insert(frame_bytes.end(), prefix.begin(), prefix.end());
        frame_bytes.insert(frame_bytes.end(), header_bytes.begin(), header_bytes.end());
        frame_bytes.insert(frame_bytes.end(), payload_and_crc.begin(), payload_and_crc.end());

        const auto parse_result = cmb::receiver::parse_frame(frame_bytes);
        if (!parse_result.ok()) {
            if (parse_result.error == cmb::receiver::ParseError::kHeaderCrc ||
                parse_result.error == cmb::receiver::ParseError::kPayloadCrc) {
                ++snapshot.crc_error_count;
            } else {
                ++snapshot.parse_fail_count;
            }
            logger.log(cmb::common::LogLevel::kWarn, "parse failed at expected frame " + std::to_string(i) + ": " + parse_result.message);
            continue;
        }
        const auto& frame = *parse_result.frame;

        const auto now = std::chrono::steady_clock::now();
        if (has_previous_receive_time) {
            recv_gap_stats.add(micros_between(previous_receive_time, now));
        }
        previous_receive_time = now;
        has_previous_receive_time = true;

        continuity.observe(frame.header.frame_id);
        if (snapshot.frame_count == 0) {
            snapshot.frame_id_begin = frame.header.frame_id;
        }
        snapshot.frame_id_end = frame.header.frame_id;
        ++snapshot.frame_count;
        if (!storage.write(frame)) {
            logger.log(cmb::common::LogLevel::kError, "failed to write frame " + std::to_string(frame.header.frame_id));
            return 7;
        }
    }

    const auto& continuity_stats = continuity.stats();
    snapshot.frame_id_begin = continuity_stats.frame_count > 0 ? continuity_stats.first_frame_id : 0;
    snapshot.frame_id_end = continuity_stats.frame_count > 0 ? continuity_stats.last_frame_id : 0;
    snapshot.frame_count = continuity_stats.frame_count;
    snapshot.recv_gap_avg_us = recv_gap_stats.average();
    snapshot.recv_gap_max_us = recv_gap_stats.max();
    snapshot.recv_gap_p999_us = recv_gap_stats.percentile(0.999);
    snapshot.tcp_disconnect_count = 0;
    metrics.append(cmb::common::now_iso8601(), snapshot);

    logger.log(cmb::common::LogLevel::kInfo, "received " + std::to_string(snapshot.frame_count) + " frames");
    std::cout << "receiver received " << snapshot.frame_count << " frames\n";
    return snapshot.frame_count == expected_frames ? 0 : 6;
}
