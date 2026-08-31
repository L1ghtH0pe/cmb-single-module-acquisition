#include "common/frame.h"
#include "common/latency_stats.h"
#include "common/logger.h"
#include "common/metrics.h"
#include "receiver/capture_queue.h"
#include "receiver/frame_parser.h"
#include "receiver/loss_detector.h"
#include "receiver/storage_writer.h"
#include "receiver/tcp_receiver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kFramePeriodUs = 5000;
constexpr std::uint64_t kRecvDeadlineSlackUs = 500;
constexpr std::uint64_t kRecvDeadlineUs = kFramePeriodUs + kRecvDeadlineSlackUs;
constexpr std::size_t kDefaultCaptureQueueFrames = 1024;
constexpr std::size_t kStorageFlushFrames = 200;

struct Options {
    std::uint16_t port{9000};
    std::uint64_t expected_frames{1000};
    std::string bind_host{"0.0.0.0"};
    std::uint16_t expected_module_id{0};
    bool validate_module_id{false};
    std::filesystem::path timing_log{};
    std::size_t capture_queue_frames{kDefaultCaptureQueueFrames};
};

std::uint64_t steady_ns(std::chrono::steady_clock::time_point time_point) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch()).count());
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
        throw std::out_of_range("expected_frame_count must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

std::size_t parse_queue_frames(const char* text) {
    const unsigned long long value = std::stoull(text);
    if (value == 0 || value > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("capture queue frame count must be positive");
    }
    return static_cast<std::size_t>(value);
}

std::uint16_t parse_module_id(const char* text) {
    const unsigned long value = std::stoul(text);
    if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::out_of_range("module_id must be in 0..65535");
    }
    return static_cast<std::uint16_t>(value);
}

void print_usage() {
    std::cerr << "usage: receiver [port] [expected_frame_count] [bind_host]"
                 " [--module-id <id>] [--timing-log <path>] [--capture-queue-frames <count>]\n";
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
            options.expected_module_id = parse_module_id(argv[index]);
            options.validate_module_id = true;
        } else if (argument == "--timing-log") {
            if (++index == argc) {
                throw std::invalid_argument("--timing-log requires a path");
            }
            options.timing_log = argv[index];
        } else if (argument == "--capture-queue-frames") {
            if (++index == argc) {
                throw std::invalid_argument("--capture-queue-frames requires a count");
            }
            options.capture_queue_frames = parse_queue_frames(argv[index]);
        } else if (argument.starts_with("--")) {
            throw std::invalid_argument("unknown option: " + argument);
        } else if (positional_index == 0) {
            options.port = parse_port(argv[index]);
            ++positional_index;
        } else if (positional_index == 1) {
            options.expected_frames = parse_frame_count(argv[index]);
            ++positional_index;
        } else if (positional_index == 2) {
            options.bind_host = argument;
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
        std::cerr << "receiver argument error: " << ex.what() << "\n";
        return 64;
    }

    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("captures/raw");
    cmb::common::Logger logger{"logs/receiver-runtime.log"};
    cmb::common::MetricsWriter metrics{"logs/receiver-metrics.csv"};
    metrics.write_header();

    std::ofstream timing;
    if (!options.timing_log.empty()) {
        timing.open(options.timing_log);
        if (!timing) {
            logger.log(cmb::common::LogLevel::kError, "failed to open receiver timing log");
            return 8;
        }
        timing << "frame_id,wire_complete_steady_ns,wire_complete_gap_us,deadline_us,late_us,deadline_miss,parse_us,queue_depth\n";
    }

    cmb::receiver::TcpReceiver receiver;
    if (!receiver.listen_on(options.port, options.bind_host)) {
        logger.log(cmb::common::LogLevel::kError, "failed to listen on " + options.bind_host + ":" + std::to_string(options.port));
        return 1;
    }

    logger.log(cmb::common::LogLevel::kInfo, "listening on " + options.bind_host + ":" + std::to_string(options.port));
    std::cout << "receiver listening on " << options.bind_host << ':' << options.port << std::endl;

    if (!receiver.accept_one()) {
        logger.log(cmb::common::LogLevel::kError, "failed to accept client");
        return 2;
    }
    logger.log(cmb::common::LogLevel::kInfo, "client accepted");

    cmb::receiver::CaptureQueue capture_queue{options.capture_queue_frames};
    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::atomic<bool> acquisition_done{false};
    std::atomic<bool> writer_failed{false};
    std::atomic<std::size_t> queue_high_water{0};
    cmb::common::LatencyStats storage_write_stats;

    std::thread capture_worker([&] {
        cmb::receiver::StorageWriter storage{"captures/raw"};
        std::size_t writes_since_flush = 0;
        for (;;) {
            auto* captured = capture_queue.consumer_slot();
            if (captured != nullptr) {
                const auto frame_id = captured->frame.header.frame_id;
                const auto write_start = std::chrono::steady_clock::now();
                const bool wrote = storage.write(captured->frame);
                storage_write_stats.add(micros_between(write_start, std::chrono::steady_clock::now()));
                capture_queue.release();
                if (!wrote) {
                    writer_failed.store(true, std::memory_order_release);
                    logger.log(cmb::common::LogLevel::kError, "failed to write capture frame " + std::to_string(frame_id));
                    break;
                }
                if (++writes_since_flush == kStorageFlushFrames) {
                    writes_since_flush = 0;
                    if (!storage.flush()) {
                        writer_failed.store(true, std::memory_order_release);
                        logger.log(cmb::common::LogLevel::kError, "failed to flush capture files");
                        break;
                    }
                }
                continue;
            }

            if (acquisition_done.load(std::memory_order_acquire)) {
                if (!storage.flush()) {
                    writer_failed.store(true, std::memory_order_release);
                    logger.log(cmb::common::LogLevel::kError, "failed to flush capture files during shutdown");
                }
                break;
            }

            std::unique_lock lock(queue_mutex);
            queue_ready.wait(lock, [&] {
                return acquisition_done.load(std::memory_order_acquire) || !capture_queue.empty();
            });
        }
    });

    cmb::common::MetricsSnapshot snapshot;
    cmb::receiver::LossDetector continuity;
    cmb::common::LatencyStats recv_gap_stats;
    cmb::common::LatencyStats parse_stats;
    std::chrono::steady_clock::time_point previous_wire_complete{};
    bool has_previous_wire_complete{false};
    std::vector<std::byte> wire_buffer(cmb::proto::kWireFrameSize);
    std::uint64_t exit_code = 0;

    for (std::uint64_t index = 0; index < options.expected_frames; ++index) {
        if (writer_failed.load(std::memory_order_acquire)) {
            logger.log(cmb::common::LogLevel::kError, "capture worker failed");
            exit_code = 7;
            break;
        }
        if (!capture_queue.has_space()) {
            ++snapshot.capture_queue_overrun_count;
            logger.log(cmb::common::LogLevel::kError, "capture queue is full before frame receive");
            exit_code = 9;
            break;
        }
        if (!receiver.receive_exact(std::span<std::byte>(wire_buffer))) {
            ++snapshot.tcp_disconnect_count;
            logger.log(cmb::common::LogLevel::kError, "failed to read complete frame");
            exit_code = 3;
            break;
        }

        const auto wire_complete = std::chrono::steady_clock::now();
        const auto wire_complete_ns = steady_ns(wire_complete);
        std::uint64_t gap_us = 0;
        std::uint64_t late_us = 0;
        bool deadline_miss = false;
        if (has_previous_wire_complete) {
            gap_us = micros_between(previous_wire_complete, wire_complete);
            recv_gap_stats.add(gap_us);
            if (gap_us > kRecvDeadlineUs) {
                deadline_miss = true;
                late_us = gap_us - kRecvDeadlineUs;
                ++snapshot.recv_deadline_miss_count;
                snapshot.recv_max_late_us = std::max(snapshot.recv_max_late_us, late_us);
            }
        }
        previous_wire_complete = wire_complete;
        has_previous_wire_complete = true;

        auto& captured = capture_queue.producer_slot();
        std::string parse_message;
        const auto parse_start = std::chrono::steady_clock::now();
        const auto parse_error = cmb::receiver::parse_frame_into(wire_buffer, captured.frame, parse_message);
        const auto parse_us = micros_between(parse_start, std::chrono::steady_clock::now());
        parse_stats.add(parse_us);
        if (parse_error != cmb::receiver::ParseError::kNone) {
            ++snapshot.parse_fail_count;
            if (parse_error == cmb::receiver::ParseError::kPayloadCrc || parse_error == cmb::receiver::ParseError::kHeaderCrc) {
                ++snapshot.crc_error_count;
            }
            logger.log(cmb::common::LogLevel::kError, "parse failed: " + parse_message);
            exit_code = 4;
            break;
        }
        if (options.validate_module_id && captured.frame.header.module_id != options.expected_module_id) {
            ++snapshot.parse_fail_count;
            logger.log(cmb::common::LogLevel::kError,
                       "unexpected module_id: " + std::to_string(captured.frame.header.module_id) +
                           ", expected " + std::to_string(options.expected_module_id));
            exit_code = 5;
            break;
        }

        continuity.observe(captured.frame.header.frame_id);
        captured.wire_complete_steady_ns = wire_complete_ns;
        capture_queue.publish();
        const auto queue_depth = capture_queue.size();
        auto high_water = queue_high_water.load(std::memory_order_relaxed);
        while (queue_depth > high_water && !queue_high_water.compare_exchange_weak(high_water, queue_depth, std::memory_order_relaxed)) {
        }
        queue_ready.notify_one();

        if (timing) {
            timing << captured.frame.header.frame_id << ',' << wire_complete_ns << ',' << gap_us << ',' << kRecvDeadlineUs << ','
                   << late_us << ',' << (deadline_miss ? 1 : 0) << ',' << parse_us << ',' << queue_depth << '\n';
        }
    }

    acquisition_done.store(true, std::memory_order_release);
    queue_ready.notify_all();
    capture_worker.join();

    if (writer_failed.load(std::memory_order_acquire) && exit_code == 0) {
        logger.log(cmb::common::LogLevel::kError, "capture worker failed");
        exit_code = 7;
    }

    const auto& continuity_stats = continuity.stats();
    snapshot.frame_id_begin = continuity_stats.frame_count > 0 ? continuity_stats.first_frame_id : 0;
    snapshot.frame_id_end = continuity_stats.frame_count > 0 ? continuity_stats.last_frame_id : 0;
    snapshot.frame_count = continuity_stats.frame_count;
    snapshot.recv_gap_avg_us = recv_gap_stats.average();
    snapshot.recv_gap_max_us = recv_gap_stats.max();
    snapshot.recv_gap_p999_us = recv_gap_stats.percentile(0.999);
    snapshot.recv_deadline_us = kRecvDeadlineUs;
    snapshot.recv_parse_avg_us = parse_stats.average();
    snapshot.recv_parse_max_us = parse_stats.max();
    snapshot.capture_queue_capacity = capture_queue.capacity();
    snapshot.capture_queue_high_water = queue_high_water.load(std::memory_order_relaxed);
    snapshot.storage_write_avg_us = storage_write_stats.average();
    snapshot.storage_write_max_us = storage_write_stats.max();
    metrics.append(cmb::common::now_iso8601(), snapshot);

    if (exit_code != 0) {
        return static_cast<int>(exit_code);
    }

    logger.log(cmb::common::LogLevel::kInfo, "received " + std::to_string(snapshot.frame_count) + " frames");
    std::cout << "receiver received " << snapshot.frame_count << " frames\n";
    return snapshot.frame_count == options.expected_frames ? 0 : 6;
}
