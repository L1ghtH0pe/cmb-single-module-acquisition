#include "common/frame.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

cmb::proto::Frame make_sample_frame() {
    cmb::proto::Frame frame;
    frame.header.module_id = 7;
    frame.header.frame_id = 42;
    frame.header.timestamp_ns = 123456789ULL;
    frame.payload.resize(cmb::proto::kChannelCount);
    for (std::size_t i = 0; i < frame.payload.size(); ++i) {
        frame.payload[i] = {static_cast<std::int32_t>(i), -static_cast<std::int32_t>(i) - 1};
    }
    return frame;
}

void test_round_trip() {
    auto frame = make_sample_frame();
    const auto bytes = cmb::proto::serialize_frame(frame);
    const auto restored = cmb::proto::deserialize_frame(bytes);

    assert(restored.header.magic == cmb::proto::kFrameMagic);
    assert(restored.header.module_id == frame.header.module_id);
    assert(restored.header.frame_id == frame.header.frame_id);
    assert(restored.header.timestamp_ns == frame.header.timestamp_ns);
    assert(restored.payload == frame.payload);
    assert(cmb::proto::validate_frame(restored).empty());
}

void test_reserialize_round_trip() {
    auto frame = make_sample_frame();
    const auto first_bytes = cmb::proto::serialize_frame(frame);
    const auto restored = cmb::proto::deserialize_frame(first_bytes);
    const auto second_bytes = cmb::proto::serialize_frame(restored);
    const auto restored_again = cmb::proto::deserialize_frame(second_bytes);

    assert(cmb::proto::validate_frame(restored_again).empty());
    assert(restored_again.header.frame_id == frame.header.frame_id);
    assert(restored_again.payload == frame.payload);
}

void test_validate_rejects_bad_magic() {
    auto frame = make_sample_frame();
    frame.header.magic = 0;
    const auto error = cmb::proto::validate_frame(frame);
    assert(!error.empty());
}

void test_deserialize_rejects_misaligned_payload_length() {
    auto frame = make_sample_frame();
    auto bytes = cmb::proto::serialize_frame(frame);

    constexpr std::size_t payload_len_offset = sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint16_t) +
        sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint64_t) +
        sizeof(std::uint64_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) +
        sizeof(std::uint16_t);
    const std::uint32_t bad_payload_len = static_cast<std::uint32_t>(cmb::proto::kPayloadBytes + 1);
    std::memcpy(bytes.data() + payload_len_offset, &bad_payload_len, sizeof(bad_payload_len));

    bool threw = false;
    try {
        (void)cmb::proto::deserialize_frame(bytes);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_wire_format_uses_little_endian() {
    auto frame = make_sample_frame();
    const auto bytes = cmb::proto::serialize_frame(frame);
    assert(bytes.size() == cmb::proto::kWireFrameSize);
    assert(bytes.size() == 4 + 44 + 1704 * 8 + 4);
    assert(static_cast<unsigned char>(bytes[0]) == 44);
    assert(static_cast<unsigned char>(bytes[1]) == 0);
    assert(static_cast<unsigned char>(bytes[4]) == 0x31);
    assert(static_cast<unsigned char>(bytes[5]) == 0x42);
    assert(static_cast<unsigned char>(bytes[6]) == 0x4D);
    assert(static_cast<unsigned char>(bytes[7]) == 0x43);
}

void test_payload_is_complex_int32_re_then_im() {
    const auto bytes = cmb::proto::serialize_frame(make_sample_frame());
    auto read_i32 = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
        }
        return static_cast<std::int32_t>(value);
    };
    constexpr std::size_t payload = cmb::proto::kFramePrefixSize + cmb::proto::kHeaderSize;
    // Channel 1: re = 1, im = -2; channel 1703 is the last 8 bytes before payload_crc.
    assert(read_i32(payload + 8) == 1);
    assert(read_i32(payload + 12) == -2);
    assert(read_i32(payload + 1703 * 8) == 1703);
    assert(read_i32(payload + 1703 * 8 + 4) == -1704);
}

void test_validate_rejects_unknown_sample_format() {
    auto frame = make_sample_frame();
    frame.header.sample_format = 0;
    const auto restored = cmb::proto::deserialize_frame(cmb::proto::serialize_frame(frame));
    assert(cmb::proto::validate_frame(restored) == "unexpected sample format");
}

}  // namespace

int main() {
    test_round_trip();
    test_reserialize_round_trip();
    test_validate_rejects_bad_magic();
    test_deserialize_rejects_misaligned_payload_length();
    test_wire_format_uses_little_endian();
    test_payload_is_complex_int32_re_then_im();
    test_validate_rejects_unknown_sample_format();
    std::cout << "frame_tests passed\n";
    return 0;
}
