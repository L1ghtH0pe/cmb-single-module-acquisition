#include "common/capture_writer.h"
#include "common/frame.h"
#include "sender/data_simulator.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::byte> read_all(const fs::path& path) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(fs::file_size(path)));
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void test_segmented_storage_keeps_wire_frames() {
    const fs::path root = "storage_writer_test/raw";
    fs::remove_all("storage_writer_test");

    std::vector<std::vector<std::byte>> frames;
    {
        cmb::common::CaptureWriter writer{root, 2};
        for (std::uint64_t id = 0; id < 3; ++id) {
            frames.push_back(cmb::proto::serialize_frame(cmb::sender::make_frame(id, 1000 + id)));
            assert(writer.write(frames.back()));
        }
        assert(writer.flush());
        assert(writer.frames_written() == 3);
    }

    assert(fs::file_size(root / "segment-000000.bin") == 2 * cmb::proto::kWireFrameSize);
    assert(fs::file_size(root / "segment-000001.bin") == cmb::proto::kWireFrameSize);
    assert(!fs::exists("storage_writer_test/meta"));

    const auto first = read_all(root / "segment-000000.bin");
    const auto second = read_all(root / "segment-000001.bin");
    assert(std::equal(frames[0].begin(), frames[0].end(), first.begin()));
    assert(std::equal(frames[1].begin(), frames[1].end(), first.begin() + cmb::proto::kWireFrameSize));
    assert(second == frames[2]);

    const auto decoded = cmb::proto::deserialize_frame(std::span<const std::byte>(first.data() + cmb::proto::kWireFrameSize, cmb::proto::kWireFrameSize));
    assert(cmb::proto::validate_frame(decoded).empty());
    assert(decoded.header.frame_id == 1);

    fs::remove_all("storage_writer_test");
}

void test_rejects_wrong_frame_size() {
    fs::remove_all("storage_writer_test");
    cmb::common::CaptureWriter writer{"storage_writer_test/raw"};
    std::vector<std::byte> short_frame(cmb::proto::kWireFrameSize - 1);
    assert(!writer.write(short_frame));
    fs::remove_all("storage_writer_test");
}

}  // namespace

int main() {
    test_segmented_storage_keeps_wire_frames();
    test_rejects_wrong_frame_size();
    std::cout << "storage_tests passed\n";
    return 0;
}
