#include "receiver/storage_writer.h"

#include <filesystem>
#include <fstream>

namespace cmb::receiver {

StorageWriter::StorageWriter(std::filesystem::path root) : root_(std::move(root)) {}

bool StorageWriter::write(const cmb::proto::Frame& frame) const {
    std::filesystem::create_directories(root_);
    const auto path = root_ / ("frame-" + std::to_string(frame.header.frame_id) + ".bin");
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(frame.payload.data()), static_cast<std::streamsize>(frame.payload.size() * sizeof(std::uint32_t)));
    return static_cast<bool>(out);
}

}  // namespace cmb::receiver
