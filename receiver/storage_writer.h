#pragma once

#include "common/frame.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace cmb::receiver {

class StorageWriter {
  public:
    explicit StorageWriter(std::filesystem::path root, std::size_t frames_per_segment = 10000);
    bool write(const cmb::proto::Frame& frame);

  private:
    bool open_segment(std::uint64_t frame_id);

    std::filesystem::path root_;
    std::filesystem::path meta_root_;
    std::size_t frames_per_segment_{10000};
    std::uint64_t current_segment_{static_cast<std::uint64_t>(-1)};
    std::uint64_t frames_in_segment_{0};
    std::uint64_t byte_offset_{0};
    std::ofstream data_{};
    std::ofstream index_{};
};

}  // namespace cmb::receiver
