#include "common/latency_stats.h"
#include "common/logger.h"
#include "common/metrics.h"
#include "sender/data_simulator.h"
#include "sender/frame_encoder.h"
#include "sender/tcp_sender.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kFramePeriodUs = 5000;
constexpr std::uint64_t kDefaultSendDeadlineUs = 5500;

struct Options {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::uint64_t frame_count{10};
    std::uint16_t module_id{0};
    std::filesystem::path timing_log{};
    std::uint64_t deadline_us{kDefaultSendDeadlineUs};
};

std::uint64_t monotonic_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t steady_ns(std::chrono::steady_clock::time_point time_point) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch()).count());
}

std::uint64_t micros_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
}

std::uint64_t late_us(std::chrono::steady_clock::time_point scheduled, std::chrono::steady_clock::time_point actual) {
    if (actual <= scheduled) {
        return 0;
    }
    return micros_between(scheduled, actual);
}

std::uint16_t parse_port(const char* text) {
    const long value = std::stol(text);
    if (value <= 0 || value > 65535) {
        throw std::out_of_range("port must be in 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

std::uint64_t parse_positive_u64(const char* text, const char* description) {
    const unsigned long long value = std::stoull(text);
    if (value == 0) {
        throw std::out_of_range(std::string(description) + " must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint16_t parse_module_id(const char* text) {
    const unsigned long value = std::stoul(text);
    if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::out_of_range("module_id must be in 0..65535");
    }
    return static_cast<std::uint16_t>(value);
}

void print_usage() {
    std::cerr << "usage: sender [host] [port] [frame_count]"
                 " [--module-id <id>] [--timing-log <path>] [--deadline-us <count>]\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    int positional_index = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--module-id") {
            if (++index == argc) {
                throw std::invalid_argument("--module-id requires a value");
            }
            options.module_id = parse_module_id(argv[index]);
        } else if (argument == "--timing-log") {
            if (++index == argc) {
                throw std::invalid_argument("--timing-log requires a path");
            }
            options.timing_log = argv[index];
        } else if (argument == "--deadline-us") {
            if (++index == argc) {
                throw std::invalid_argument("--deadline-us requires a count");
            }
            options.deadline_us = parse_positive_u64(argv[index], "send deadline");
        } else if (argument.starts_with("--")) {
            throw std::invalid_argument("unknown option: " + argument);
        } else if (positional_index == 0) {
            options.host = argument;
            ++positional_index;
        } else if (positional_index == 1) {
            options.port = parse_port(argv[index]);
            ++positional_index;
        } else if (positional_index == 2) {
            options.frame_count = parse_positive_u64(argv[index], "frame_count");
            ++positional_index;
        } else {
            throw std::invalid_argument("too many positional arguments");
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
    } catch (const std::exception& ex) {
        print_usage();
        std::cerr << "sender argument error: " << ex.what() << "\n";
        return 64;
    }

    std::filesystem::create_directories("logs");
    cmb::common::Logger logger{"logs/sender-runtime.log"};
    cmb::common::MetricsWriter metrics{"logs/sender-metrics.csv"};
    metrics.write_header();

    std::ofstream timing;
    if (!options.timing_log.empty()) {
        timing.open(options.timing_log);
        if (!timing) {
            logger.log(cmb::common::LogLevel::kError, "failed to open sender timing log");
            return 8;
        }
        timing << "frame_id,scheduled_steady_ns,send_start_steady_ns,send_end_steady_ns,"
                  "schedule_late_us,work_us,encode_us,socket_send_us,deadline_us,deadline_miss\n";
    }

    cmb::sender::TcpSender sender;
    if (!sender.connect_to(options.host, options.port)) {
        logger.log(cmb::common::LogLevel::kError, "failed to connect to " + options.host + ":" + std::to_string(options.port));
        return 1;
    }

    logger.log(cmb::common::LogLevel::kInfo, "connected to " + options.host + ":" + std::to_string(options.port));

    cmb::common::LatencyStats send_period_stats;
    cmb::common::LatencyStats schedule_late_stats;
    cmb::common::LatencyStats send_work_stats;
    cmb::common::LatencyStats encode_stats;
    cmb::common::LatencyStats socket_send_stats;
    cmb::proto::Frame frame;
    frame.payload.resize(cmb::proto::kChannelCount);
    std::vector<std::byte> bytes;
    auto next_tick = std::chrono::steady_clock::now();
    auto previous_start = next_tick;
    std::uint64_t deadline_miss_count = 0;
    std::uint64_t max_late_us = 0;

    for (std::uint64_t frame_id = 0; frame_id < options.frame_count; ++frame_id) {
        const auto scheduled = next_tick;
        std::this_thread::sleep_until(scheduled);
        const auto send_start = std::chrono::steady_clock::now();
        const auto schedule_late_us = late_us(scheduled, send_start);
        const auto encode_start = send_start;
        cmb::sender::fill_frame(frame, frame_id, monotonic_ns(), options.module_id);
        cmb::sender::encode_frame_into(frame, bytes);
        const auto socket_send_start = std::chrono::steady_clock::now();
        const auto encode_us = micros_between(encode_start, socket_send_start);
        if (!sender.send(std::span<const std::byte>(bytes))) {
            logger.log(cmb::common::LogLevel::kError, "failed to send frame " + std::to_string(frame_id));
            return 2;
        }
        const auto send_end = std::chrono::steady_clock::now();
        const auto socket_send_us = micros_between(socket_send_start, send_end);
        const auto work_us = micros_between(send_start, send_end);
        const bool deadline_miss = schedule_late_us > options.deadline_us;

        if (frame_id > 0) {
            send_period_stats.add(micros_between(previous_start, send_start));
        }
        previous_start = send_start;
        schedule_late_stats.add(schedule_late_us);
        send_work_stats.add(work_us);
        encode_stats.add(encode_us);
        socket_send_stats.add(socket_send_us);
        max_late_us = std::max(max_late_us, schedule_late_us);
        if (deadline_miss) {
            ++deadline_miss_count;
        }

        if (timing) {
            timing << frame_id << ','
                   << steady_ns(scheduled) << ','
                   << steady_ns(send_start) << ','
                   << steady_ns(send_end) << ','
                   << schedule_late_us << ','
                   << work_us << ','
                   << encode_us << ','
                   << socket_send_us << ','
                   << options.deadline_us << ','
                   << (deadline_miss ? 1 : 0) << '\n';
        }

        next_tick += std::chrono::microseconds(kFramePeriodUs);
    }

    if (timing) {
        timing.flush();
        if (!timing) {
            logger.log(cmb::common::LogLevel::kError, "failed to write sender timing log");
            return 8;
        }
    }

    cmb::common::MetricsSnapshot snapshot;
    snapshot.frame_id_begin = 0;
    snapshot.frame_id_end = options.frame_count - 1;
    snapshot.frame_count = sender.sent_frames();
    snapshot.send_period_avg_us = send_period_stats.average();
    snapshot.send_period_max_us = send_period_stats.max();
    snapshot.send_period_p999_us = send_period_stats.percentile(0.999);
    snapshot.send_deadline_us = options.deadline_us;
    snapshot.send_deadline_miss_count = deadline_miss_count;
    snapshot.send_max_late_us = max_late_us;
    snapshot.send_schedule_late_avg_us = schedule_late_stats.average();
    snapshot.send_schedule_late_max_us = schedule_late_stats.max();
    snapshot.send_work_avg_us = send_work_stats.average();
    snapshot.send_work_max_us = send_work_stats.max();
    snapshot.send_encode_avg_us = encode_stats.average();
    snapshot.send_encode_max_us = encode_stats.max();
    snapshot.send_socket_avg_us = socket_send_stats.average();
    snapshot.send_socket_max_us = socket_send_stats.max();
    metrics.append(cmb::common::now_iso8601(), snapshot);

    logger.log(cmb::common::LogLevel::kInfo, "sent " + std::to_string(sender.sent_frames()) + " frames");
    std::cout << "sender sent " << sender.sent_frames() << " frames\n";
    return 0;
}
