// frame_gen: write a source .bin of simulated wire frames for sender --input.
//
// usage: frame_gen <output.bin> <frame_count> [--module-id <id>] [--pattern counter|random] [--seed <n>] [--rate-hz <n>]
//
// counter: same deterministic values as the live sender (re = frame_id + channel, im = ~re).
// random:  pseudo-random int32 re/im from a seeded xorshift64*, so every bit position is exercised.
// timestamp_ns is set to frame_id / rate_hz seconds so the file is reproducible.

#include "common/capture_writer.h"
#include "common/frame.h"
#include "sender/data_simulator.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path output;
    std::uint64_t frames{0};
    std::uint16_t module_id{0};
    bool random{false};
    std::uint64_t seed{0x434D4231ULL};
    std::uint16_t rate_hz{cmb::proto::kSampleRateHz};
};

std::uint64_t xorshift64star(std::uint64_t& state) {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1DULL;
}

Options parse(int argc, char** argv) {
    Options options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--module-id" && i + 1 < argc) {
            const auto value = std::stoul(argv[++i]);
            if (value > std::numeric_limits<std::uint16_t>::max()) {
                throw std::out_of_range("module_id must be in 0..65535");
            }
            options.module_id = static_cast<std::uint16_t>(value);
        } else if (arg == "--pattern" && i + 1 < argc) {
            const std::string pattern = argv[++i];
            if (pattern != "counter" && pattern != "random") {
                throw std::invalid_argument("pattern must be counter or random");
            }
            options.random = pattern == "random";
        } else if (arg == "--rate-hz" && i + 1 < argc) {
            const auto value = std::stoul(argv[++i]);
            if (value == 0 || value > cmb::proto::kMaxSampleRateHz) {
                throw std::out_of_range("rate_hz must be in 1.." + std::to_string(cmb::proto::kMaxSampleRateHz));
            }
            options.rate_hz = static_cast<std::uint16_t>(value);
        } else if (arg == "--seed" && i + 1 < argc) {
            options.seed = std::stoull(argv[++i]);
        } else if (arg.starts_with("--")) {
            throw std::invalid_argument("unknown option: " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2) {
        throw std::invalid_argument("expected <output.bin> <frame_count>");
    }
    options.output = positional[0];
    options.frames = std::stoull(positional[1]);
    if (options.frames == 0) {
        throw std::invalid_argument("frame_count must be positive");
    }
    if (options.seed == 0) {
        options.seed = 1;  // xorshift state must be nonzero
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "usage: frame_gen <output.bin> <frame_count> [--module-id <id>] [--pattern counter|random] [--seed <n>] [--rate-hz <n>]\n"
                  << "frame_gen argument error: " << ex.what() << "\n";
        return 64;
    }

    // Write a single file: frames_per_segment is set to the total frame count.
    const auto parent = options.output.parent_path().empty() ? std::filesystem::path(".") : options.output.parent_path();
    const auto tmp_dir = parent / (options.output.filename().string() + ".tmp");
    std::filesystem::remove_all(tmp_dir);

    std::uint64_t state = options.seed;
    cmb::proto::Frame frame;
    std::vector<std::byte> bytes;
    {
        cmb::common::CaptureWriter writer{tmp_dir, static_cast<std::size_t>(options.frames)};
        for (std::uint64_t id = 0; id < options.frames; ++id) {
            cmb::sender::fill_frame(frame, id, id * 1'000'000'000ULL / options.rate_hz, options.module_id, options.rate_hz);
            if (options.random) {
                for (auto& sample : frame.payload) {
                    const auto word = xorshift64star(state);
                    sample = {static_cast<std::int32_t>(static_cast<std::uint32_t>(word)),
                              static_cast<std::int32_t>(static_cast<std::uint32_t>(word >> 32))};
                }
            }
            cmb::proto::serialize_frame_into(frame, bytes);
            if (!writer.write(bytes)) {
                std::cerr << "frame_gen: failed to write frame " << id << "\n";
                return 1;
            }
        }
        if (!writer.flush()) {
            std::cerr << "frame_gen: failed to flush output\n";
            return 1;
        }
    }
    std::filesystem::rename(tmp_dir / "segment-000000.bin", options.output);
    std::filesystem::remove_all(tmp_dir);

    std::cout << "frame_gen wrote " << options.frames << " frames (" << options.frames * cmb::proto::kWireFrameSize
              << " bytes) to " << options.output.string() << "\n";
    return 0;
}
