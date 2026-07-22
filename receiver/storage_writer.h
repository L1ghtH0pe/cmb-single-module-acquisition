#pragma once

#include "common/frame.h"

#include <filesystem>

namespace cmb::receiver {

class StorageWriter {
  public:
    explicit StorageWriter(std::filesystem::path root);
    bool write(const cmb::proto::Frame& frame) const;

  private:
    std::filesystem::path root_;
};

}  // namespace cmb::receiver
