// frame_compare: verify that two captures of wire frames are byte-identical.
//
// usage: frame_compare <expected.bin|dir> <actual.bin|dir> [--max-report <n>]
//
// Typical pairs: frame_gen source file vs receiver captures/raw, or sender --save-sent
// vs receiver captures/raw. Each side is one .bin file or a directory of *.bin files
// (read in name order). Both sides are streamed frame by frame and merged by frame_id,
// so memory use is two frames regardless of capture length. Every frame is decoded and
// CRC-checked, then compared byte for byte. Exit code 0 means every frame matches.

#include "common/capture_reader.h"
#include "common/frame.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::string field_at(std::size_t offset) {
    using namespace cmb::proto;
    constexpr std::size_t payload_begin = kFramePrefixSize + kHeaderSize;
    constexpr std::size_t payload_end = payload_begin + kPayloadBytes;
    if (offset < kFramePrefixSize) {
        return "header_len";
    }
    if (offset < payload_begin) {
        struct Field {
            std::size_t end;
            const char* name;
        };
        static constexpr Field fields[] = {
            {8, "magic"},          {10, "version"},        {12, "header_size"},   {14, "module_id"},
            {16, "flags"},         {24, "frame_id"},       {32, "timestamp_ns"},  {34, "channel_count"},
            {36, "sample_rate_hz"}, {38, "sample_format"}, {40, "reserved"},      {44, "payload_len"},
            {48, "header_crc"},
        };
        for (const auto& field : fields) {
            if (offset < field.end) {
                return std::string("header.") + field.name;
            }
        }
    }
    if (offset < payload_end) {
        const auto relative = offset - payload_begin;
        return "payload[channel=" + std::to_string(relative / kSampleBytes) + "]." +
               (relative % kSampleBytes < 4 ? "re" : "im");
    }
    return "payload_crc";
}

struct SideStats {
    std::uint64_t total{0};
    std::uint64_t invalid{0};
    std::uint64_t out_of_order{0};  // frame_id not strictly increasing (includes duplicates)
};

// One side of the comparison: streams frames and remembers the current one.
class Side {
  public:
    Side(const char* label, const std::string& path) : label_(label), reader_(path), wire_(cmb::proto::kWireFrameSize) {}

    bool ok() const { return reader_.ok(); }
    std::string error() const { return reader_.error(); }
    const char* label() const { return label_; }

    // Advances to the next frame. Returns false at end of input.
    template <typename Report>
    bool advance(Report&& report) {
        if (!reader_.next(wire_)) {
            has_frame_ = false;
            return false;
        }
        has_frame_ = true;
        ++stats_.total;
        problem_.clear();
        try {
            cmb::proto::deserialize_frame_into(wire_, frame_);
            frame_id_ = frame_.header.frame_id;
            problem_ = cmb::proto::validate_frame(frame_);
        } catch (const std::exception& ex) {
            problem_ = ex.what();
            frame_id_ = raw_frame_id();
        }
        if (!problem_.empty()) {
            ++stats_.invalid;
            report(std::string("INVALID ") + label_ + " frame_id=" + std::to_string(frame_id_) + " at " + location() + ": " +
                   problem_);
        }
        if (last_frame_id_ && frame_id_ <= *last_frame_id_) {
            ++stats_.out_of_order;
            report(std::string("OUT_OF_ORDER ") + label_ + " frame_id=" + std::to_string(frame_id_) + " after " +
                   std::to_string(*last_frame_id_) + " at " + location());
        }
        last_frame_id_ = frame_id_;
        return true;
    }

    bool has_frame() const { return has_frame_; }
    bool valid() const { return problem_.empty(); }
    std::uint64_t frame_id() const { return frame_id_; }
    const std::vector<std::byte>& wire() const { return wire_; }
    std::string location() const { return reader_.location(); }
    const SideStats& stats() const { return stats_; }
    std::uint64_t trailing_bytes() const { return reader_.trailing_bytes(); }

  private:
    // Used when the header cannot be decoded, so a damaged frame can still be paired.
    std::uint64_t raw_frame_id() const {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(wire_[cmb::proto::kFrameIdWireOffset + i])) << (8 * i);
        }
        return value;
    }

    const char* label_;
    cmb::common::CaptureReader reader_;
    std::vector<std::byte> wire_;
    cmb::proto::Frame frame_;
    bool has_frame_{false};
    std::uint64_t frame_id_{0};
    std::optional<std::uint64_t> last_frame_id_;
    std::string problem_;
    SideStats stats_;
};

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    std::uint64_t max_report = 20;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--max-report" && i + 1 < argc) {
            max_report = std::stoull(argv[++i]);
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2) {
        std::cerr << "usage: frame_compare <expected.bin|dir> <actual.bin|dir> [--max-report <n>]\n";
        return 64;
    }

    Side expected{"expected", positional[0]};
    Side actual{"actual", positional[1]};
    for (const auto* side : {&expected, &actual}) {
        if (!side->ok()) {
            std::cerr << side->label() << ": " << side->error() << "\n";
            return 2;
        }
    }

    std::uint64_t identical = 0;
    std::uint64_t mismatched = 0;
    std::uint64_t missing = 0;
    std::uint64_t unexpected = 0;
    std::uint64_t reported = 0;
    auto report = [&](const std::string& line) {
        if (reported++ < max_report) {
            std::cout << line << "\n";
        }
    };

    expected.advance(report);
    actual.advance(report);
    while (expected.has_frame() || actual.has_frame()) {
        // A frame that fails CRC has an untrustworthy frame_id; pair it with the other side's
        // current frame by position so a corrupted id does not desynchronize the merge.
        const bool both = expected.has_frame() && actual.has_frame();
        const bool by_position = both && expected.frame_id() != actual.frame_id() && (!expected.valid() || !actual.valid());
        if (!by_position && (!actual.has_frame() || (expected.has_frame() && expected.frame_id() < actual.frame_id()))) {
            ++missing;
            report("MISSING frame_id=" + std::to_string(expected.frame_id()) + " (expected " + expected.location() + ")");
            expected.advance(report);
            continue;
        }
        if (!by_position && (!expected.has_frame() || actual.frame_id() < expected.frame_id())) {
            ++unexpected;
            report("UNEXPECTED frame_id=" + std::to_string(actual.frame_id()) + " only in actual " + actual.location());
            actual.advance(report);
            continue;
        }

        const auto& a = expected.wire();
        const auto& b = actual.wire();
        std::size_t first = a.size();
        std::size_t differing = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                if (differing++ == 0) {
                    first = i;
                }
            }
        }
        if (differing == 0) {
            ++identical;
        } else {
            ++mismatched;
            char values[48];
            std::snprintf(values, sizeof(values), "expected=0x%02x actual=0x%02x", static_cast<unsigned>(a[first]),
                          static_cast<unsigned>(b[first]));
            report("MISMATCH frame_id=" + std::to_string(expected.frame_id()) + " first_offset=" + std::to_string(first) +
                   " field=" + field_at(first) + " " + values + " differing_bytes=" + std::to_string(differing) +
                   (by_position ? " actual_frame_id=" + std::to_string(actual.frame_id()) + "(paired by position)" : "") +
                   " (expected " + expected.location() + ", actual " + actual.location() + ")");
        }
        expected.advance(report);
        actual.advance(report);
    }
    if (reported > max_report) {
        std::cout << "... " << (reported - max_report) << " more issue lines suppressed\n";
    }

    const auto& es = expected.stats();
    const auto& as = actual.stats();
    const bool ok = mismatched == 0 && missing == 0 && unexpected == 0 && es.invalid == 0 && as.invalid == 0 &&
                    es.out_of_order == 0 && as.out_of_order == 0 && expected.trailing_bytes() == 0 &&
                    actual.trailing_bytes() == 0 && es.total > 0;

    std::cout << "frame_bytes=" << cmb::proto::kWireFrameSize << "\n"
              << "expected_frames=" << es.total << " invalid=" << es.invalid << " out_of_order=" << es.out_of_order
              << " trailing_bytes=" << expected.trailing_bytes() << "\n"
              << "actual_frames=" << as.total << " invalid=" << as.invalid << " out_of_order=" << as.out_of_order
              << " trailing_bytes=" << actual.trailing_bytes() << "\n"
              << "identical=" << identical << " mismatched=" << mismatched << " missing=" << missing
              << " unexpected=" << unexpected << "\n"
              << "result=" << (ok ? "IDENTICAL" : "DIFFERENT") << "\n";
    return ok ? 0 : 1;
}
