#include "common/capture_writer.h"

#include "common/frame.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace cmb::common {
namespace {

std::string segment_name(std::uint64_t segment_id) {
    std::ostringstream oss;
    oss << "segment-" << std::setw(6) << std::setfill('0') << segment_id << ".bin";
    return oss.str();
}

}  // namespace

CaptureWriter::CaptureWriter(std::filesystem::path root, std::size_t frames_per_segment)
    : root_(std::move(root)), frames_per_segment_(frames_per_segment) {}

CaptureWriter::~CaptureWriter() {
    flush();
}

bool CaptureWriter::open_segment(std::uint64_t segment_id) {
    if (segment_id == current_segment_ && data_) {
        return true;
    }

    data_.close();
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    data_.open(root_ / segment_name(segment_id), std::ios::binary | std::ios::trunc);
    if (!data_) {
        return false;
    }
    current_segment_ = segment_id;
    return true;
}

bool CaptureWriter::write(std::span<const std::byte> wire_frame) {
    if (frames_per_segment_ == 0 || wire_frame.size() != cmb::proto::kWireFrameSize ||
        !open_segment(frames_written_ / frames_per_segment_)) {
        return false;
    }

    data_.write(reinterpret_cast<const char*>(wire_frame.data()), static_cast<std::streamsize>(wire_frame.size()));
    if (!data_) {
        return false;
    }
    ++frames_written_;
    return true;
}

bool CaptureWriter::flush() {
    if (data_.is_open()) {
        data_.flush();
    }
    return !data_.is_open() || static_cast<bool>(data_);
}

}  // namespace cmb::common
