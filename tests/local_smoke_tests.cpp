#include "common/frame.h"
#include "receiver/frame_parser.h"
#include "receiver/tcp_receiver.h"
#include "sender/data_simulator.h"
#include "sender/frame_encoder.h"
#include "sender/tcp_sender.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace {

std::uint32_t read_payload_len(cmb::receiver::TcpReceiver& receiver) {
    std::vector<std::byte> prefix;
    assert(receiver.receive_exact(prefix, sizeof(std::uint32_t)));
    const auto header_len = cmb::receiver::peek_header_len(prefix);

    std::vector<std::byte> header;
    assert(receiver.receive_exact(header, header_len));
    return cmb::receiver::peek_payload_len(header);
}

cmb::proto::Frame receive_one_frame(cmb::receiver::TcpReceiver& receiver) {
    std::vector<std::byte> prefix;
    assert(receiver.receive_exact(prefix, sizeof(std::uint32_t)));
    const auto header_len = cmb::receiver::peek_header_len(prefix);

    std::vector<std::byte> header;
    assert(receiver.receive_exact(header, header_len));
    const auto payload_len = cmb::receiver::peek_payload_len(header);

    std::vector<std::byte> payload_and_crc;
    assert(receiver.receive_exact(payload_and_crc, payload_len + sizeof(std::uint32_t)));

    std::vector<std::byte> frame_bytes;
    frame_bytes.reserve(prefix.size() + header.size() + payload_and_crc.size());
    frame_bytes.insert(frame_bytes.end(), prefix.begin(), prefix.end());
    frame_bytes.insert(frame_bytes.end(), header.begin(), header.end());
    frame_bytes.insert(frame_bytes.end(), payload_and_crc.begin(), payload_and_crc.end());

    auto result = cmb::receiver::parse_frame(frame_bytes);
    assert(result.ok());
    return *result.frame;
}

void test_localhost_transfers_three_frames() {
    constexpr std::uint16_t port = 9100;
    constexpr std::uint64_t frames = 3;

    std::exception_ptr receiver_error;
    std::vector<std::uint64_t> received_ids;

    std::thread receiver_thread([&] {
        try {
            cmb::receiver::TcpReceiver receiver;
            assert(receiver.listen_on(port));
            assert(receiver.accept_one());
            for (std::uint64_t i = 0; i < frames; ++i) {
                auto frame = receive_one_frame(receiver);
                assert(cmb::proto::validate_frame(frame).empty());
                received_ids.push_back(frame.header.frame_id);
            }
        } catch (...) {
            receiver_error = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cmb::sender::TcpSender sender;
    assert(sender.connect_to("127.0.0.1", port));
    for (std::uint64_t i = 0; i < frames; ++i) {
        auto frame = cmb::sender::make_frame(i, 1000 + i);
        auto bytes = cmb::sender::encode_frame(frame);
        assert(sender.send(bytes));
    }

    receiver_thread.join();
    if (receiver_error) {
        std::rethrow_exception(receiver_error);
    }

    assert(sender.sent_frames() == frames);
    assert(received_ids.size() == frames);
    assert(received_ids[0] == 0);
    assert(received_ids[1] == 1);
    assert(received_ids[2] == 2);
}

}  // namespace

int main() {
    test_localhost_transfers_three_frames();
    std::cout << "local_smoke_tests passed\n";
    return 0;
}
