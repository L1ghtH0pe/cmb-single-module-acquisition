#include "receiver/storage_writer.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace cmb::receiver {
namespace {

std::string segment_name(std::uint64_t segment_id, const char* extension) {
    std::ostringstream oss;
    oss << "segment-" << std::setw(6) << std::setfill('0') << segment_id << extension;
    return oss.str();
}

}  // namespace

StorageWriter::StorageWriter(std::filesystem::path root, std::size_t frames_per_segment)
    : root_(std::move(root)), meta_root_(root_.parent_path() / "meta"), frames_per_segment_(frames_per_segment) {}

StorageWriter::~StorageWriter() {
    flush();
}

bool StorageWriter::open_segment(std::uint64_t frame_id) {
    const auto segment_id = frame_id / frames_per_segment_;
    if (segment_id == current_segment_ && data_ && index_) {
        return true;
    }

    data_.close();
    index_.close();

    std::filesystem::create_directories(root_);
    std::filesystem::create_directories(meta_root_);

    const auto data_path = root_ / segment_name(segment_id, ".bin");
    const auto index_path = meta_root_ / segment_name(segment_id, ".csv");
    const bool new_index = !std::filesystem::exists(index_path);

    data_.open(data_path, std::ios::binary | std::ios::app);
    index_.open(index_path, std::ios::app);
    if (!data_ || !index_) {
        return false;
    }

    if (new_index) {
        index_ << "frame_id,timestamp_ns,offset,payload_bytes\n";
    }

    current_segment_ = segment_id;
    frames_in_segment_ = 0;
    byte_offset_ = static_cast<std::uint64_t>(std::filesystem::file_size(data_path));
    return true;
}

bool StorageWriter::write(const cmb::proto::Frame& frame) {
    if (frames_per_segment_ == 0 || !open_segment(frame.header.frame_id)) {
        return false;
    }

    const auto payload_bytes = static_cast<std::uint64_t>(frame.payload.size() * sizeof(std::uint32_t));
    data_.write(reinterpret_cast<const char*>(frame.payload.data()), static_cast<std::streamsize>(payload_bytes));
    if (!data_) {
        return false;
    }

    index_ << frame.header.frame_id << ',' << frame.header.timestamp_ns << ',' << byte_offset_ << ',' << payload_bytes << '\n';
    if (!index_ || !data_) {
        return false;
    }

    byte_offset_ += payload_bytes;
    ++frames_in_segment_;
    return true;
}

bool StorageWriter::flush() {
    if (data_) {
        data_.flush();
    }
    if (index_) {
        index_.flush();
    }
    return (!data_.is_open() || static_cast<bool>(data_)) && (!index_.is_open() || static_cast<bool>(index_));
}

}  // namespace cmb::receiver
