#include "common/latency_stats.h"
#include "common/logger.h"
#include "common/metrics.h"
#include "sender/data_simulator.h"
#include "sender/frame_encoder.h"
#include "sender/tcp_sender.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::uint64_t monotonic_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

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
        throw std::out_of_range("frame_count must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

void print_usage() {
    std::cerr << "usage: sender [host] [port] [frame_count]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::uint64_t frame_count = 10;
    try {
        host = argc > 1 ? argv[1] : host;
        port = argc > 2 ? parse_port(argv[2]) : port;
        frame_count = argc > 3 ? parse_frame_count(argv[3]) : frame_count;
    } catch (const std::exception& ex) {
        print_usage();
        std::cerr << "sender argument error: " << ex.what() << "\n";
        return 64;
    }

    std::filesystem::create_directories("logs");
    cmb::common::Logger logger{"logs/sender-runtime.log"};
    cmb::common::MetricsWriter metrics{"logs/sender-metrics.csv"};
    metrics.write_header();

    cmb::sender::TcpSender sender;
    if (!sender.connect_to(host, port)) {
        logger.log(cmb::common::LogLevel::kError, "failed to connect to " + host + ":" + std::to_string(port));
        return 1;
    }

    logger.log(cmb::common::LogLevel::kInfo, "connected to " + host + ":" + std::to_string(port));

    cmb::common::LatencyStats send_period_stats;
    auto next_tick = std::chrono::steady_clock::now();
    auto previous_tick = next_tick;
    for (std::uint64_t frame_id = 0; frame_id < frame_count; ++frame_id) {
        next_tick += std::chrono::milliseconds(5);
        auto frame = cmb::sender::make_frame(frame_id, monotonic_ns());
        auto bytes = cmb::sender::encode_frame(frame);
        if (!sender.send(bytes)) {
            logger.log(cmb::common::LogLevel::kError, "send failed at frame " + std::to_string(frame_id));
            return 2;
        }

        const auto now = std::chrono::steady_clock::now();
        if (frame_id > 0) {
            send_period_stats.add(micros_between(previous_tick, now));
        }
        previous_tick = now;
        std::this_thread::sleep_until(next_tick);
    }

    cmb::common::MetricsSnapshot snapshot;
    snapshot.frame_id_begin = 0;
    snapshot.frame_id_end = frame_count == 0 ? 0 : frame_count - 1;
    snapshot.frame_count = sender.sent_frames();
    snapshot.send_period_avg_us = send_period_stats.average();
    snapshot.send_period_max_us = send_period_stats.max();
    snapshot.send_period_p999_us = send_period_stats.percentile(0.999);
    metrics.append(cmb::common::now_iso8601(), snapshot);

    logger.log(cmb::common::LogLevel::kInfo, "sent " + std::to_string(sender.sent_frames()) + " frames");
    std::cout << "sender sent " << sender.sent_frames() << " frames\n";
    return 0;
}
