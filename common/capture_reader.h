#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace cmb::common {

// Reads wire frames (kWireFrameSize bytes each) in order from one .bin file or from
// every *.bin file in a directory, sorted by file name.
class CaptureReader {
  public:
    explicit CaptureReader(const std::filesystem::path& input);

    // False if the input does not exist or contains no .bin files.
    bool ok() const { return !files_.empty(); }

    // Reads the next complete frame into out. Returns false at end of input.
    // A partial frame at the end of a file is skipped and counted in trailing_bytes().
    bool next(std::span<std::byte> out);

    // "segment-000000.bin:17" for the frame returned by the last next() call.
    std::string location() const;
    std::uint64_t trailing_bytes() const { return trailing_bytes_; }
    std::string error() const { return error_; }

  private:
    bool open_next_file();

    std::vector<std::filesystem::path> files_;
    std::size_t file_index_{0};
    std::ifstream in_;
    std::uint64_t frame_in_file_{0};
    std::uint64_t trailing_bytes_{0};
    std::string current_name_;
    std::string error_;
};

}  // namespace cmb::common
