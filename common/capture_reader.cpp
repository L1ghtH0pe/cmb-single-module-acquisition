#include "common/capture_reader.h"

#include "common/frame.h"

#include <algorithm>

namespace cmb::common {

CaptureReader::CaptureReader(const std::filesystem::path& input) {
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(input, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bin") {
                files_.push_back(entry.path());
            }
        }
        std::sort(files_.begin(), files_.end());
    } else if (std::filesystem::is_regular_file(input, ec)) {
        files_.push_back(input);
    }
    if (files_.empty()) {
        error_ = "no .bin files found at " + input.string();
    }
}

bool CaptureReader::open_next_file() {
    in_.close();
    while (file_index_ < files_.size()) {
        const auto& path = files_[file_index_++];
        in_.open(path, std::ios::binary);
        if (in_) {
            current_name_ = path.filename().string();
            frame_in_file_ = 0;
            return true;
        }
        error_ = "cannot open " + path.string();
        in_.clear();
    }
    return false;
}

bool CaptureReader::next(std::span<std::byte> out) {
    if (out.size() != cmb::proto::kWireFrameSize) {
        return false;
    }
    for (;;) {
        if (!in_.is_open() && !open_next_file()) {
            return false;
        }
        in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        const auto got = static_cast<std::size_t>(in_.gcount());
        if (got == out.size()) {
            ++frame_in_file_;
            return true;
        }
        trailing_bytes_ += got;
        in_.close();
    }
}

std::string CaptureReader::location() const {
    return current_name_ + ":" + std::to_string(frame_in_file_ == 0 ? 0 : frame_in_file_ - 1);
}

}  // namespace cmb::common
