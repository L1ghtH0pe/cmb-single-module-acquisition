#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

namespace cmb::common {

// Appends complete wire frames (kWireFrameSize bytes each) to segment-XXXXXX.bin files.
// The files carry no extra index: every frame is self-describing (header_len, header, payload, payload_crc).
class CaptureWriter {
  public:
    explicit CaptureWriter(std::filesystem::path root, std::size_t frames_per_segment = 10000);
    ~CaptureWriter();

    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    bool write(std::span<const std::byte> wire_frame);
    bool flush();
    std::uint64_t frames_written() const { return frames_written_; }

  private:
    bool open_segment(std::uint64_t segment_id);

    std::filesystem::path root_;
    std::size_t frames_per_segment_{10000};
    std::uint64_t current_segment_{static_cast<std::uint64_t>(-1)};
    std::uint64_t frames_written_{0};
    std::ofstream data_{};
};

}  // namespace cmb::common
