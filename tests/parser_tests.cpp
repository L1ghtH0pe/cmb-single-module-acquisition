#include "common/frame.h"
#include "receiver/frame_parser.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

cmb::proto::Frame make_sample_frame() {
    cmb::proto::Frame frame;
    frame.header.frame_id = 1;
    frame.header.timestamp_ns = 100;
    frame.payload.resize(cmb::proto::kChannelCount);
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        frame.payload[i] = static_cast<std::uint32_t>(i);
    }
    return frame;
}

void test_parser_success() {
    const auto bytes = cmb::proto::serialize_frame(make_sample_frame());
    const auto result = cmb::receiver::parse_frame(bytes);
    assert(result.ok());
    assert(result.error == cmb::receiver::ParseError::kNone);
    assert(result.frame->header.frame_id == 1);
}

void test_parser_classifies_payload_crc() {
    auto bytes = cmb::proto::serialize_frame(make_sample_frame());
    bytes[bytes.size() - 1] = static_cast<std::byte>(~static_cast<unsigned char>(bytes[bytes.size() - 1]));

    const auto result = cmb::receiver::parse_frame(bytes);
    assert(!result.ok());
    assert(result.error == cmb::receiver::ParseError::kPayloadCrc);
}

void test_parser_classifies_header_crc() {
    auto bytes = cmb::proto::serialize_frame(make_sample_frame());
    constexpr std::size_t module_id_offset = sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t);
    bytes[module_id_offset] = static_cast<std::byte>(0x7F);

    const auto result = cmb::receiver::parse_frame(bytes);
    assert(!result.ok());
    assert(result.error == cmb::receiver::ParseError::kHeaderCrc);
}

}  // namespace

int main() {
    test_parser_success();
    test_parser_classifies_payload_crc();
    test_parser_classifies_header_crc();
    std::cout << "parser_tests passed\n";
    return 0;
}
