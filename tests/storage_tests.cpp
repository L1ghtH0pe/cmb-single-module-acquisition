#include "receiver/storage_writer.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

cmb::proto::Frame make_frame(std::uint64_t frame_id) {
    cmb::proto::Frame frame;
    frame.header.frame_id = frame_id;
    frame.header.timestamp_ns = 1000 + frame_id;
    frame.payload.resize(cmb::proto::kChannelCount);
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        frame.payload[i] = static_cast<std::uint32_t>(frame_id + i);
    }
    return frame;
}

std::size_t count_data_rows(const fs::path& path) {
    std::ifstream in(path);
    std::string line;
    std::size_t rows = 0;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            continue;
        }
        if (!line.empty()) {
            ++rows;
        }
    }
    return rows;
}

void test_segmented_storage() {
    const fs::path root = "storage_writer_test/raw";
    fs::remove_all("storage_writer_test");

    {
        cmb::receiver::StorageWriter writer{root, 2};
        assert(writer.write(make_frame(0)));
        assert(writer.write(make_frame(1)));
        assert(writer.write(make_frame(2)));
        assert(writer.flush());

        assert(fs::file_size(root / "segment-000000.bin") == 2 * cmb::proto::kPayloadBytes);
        assert(fs::file_size(root / "segment-000001.bin") == cmb::proto::kPayloadBytes);
        assert(count_data_rows("storage_writer_test/meta/segment-000000.csv") == 2);
        assert(count_data_rows("storage_writer_test/meta/segment-000001.csv") == 1);
    }

    fs::remove_all("storage_writer_test");
}

}  // namespace

int main() {
    test_segmented_storage();
    std::cout << "storage_tests passed\n";
    return 0;
}
