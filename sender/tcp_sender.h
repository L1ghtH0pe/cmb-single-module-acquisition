#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cmb::sender {

class TcpSender {
  public:
#ifdef _WIN32
    using SocketHandle = std::uintptr_t;
    static constexpr SocketHandle kInvalidSocket = ~SocketHandle{0};
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

    TcpSender();
    ~TcpSender();

    TcpSender(const TcpSender&) = delete;
    TcpSender& operator=(const TcpSender&) = delete;

    bool connect_to(const std::string& host, std::uint16_t port);
    bool send(const std::vector<std::byte>& bytes);
    void close();

    std::uint64_t sent_frames() const { return sent_frames_; }
    bool connected() const { return socket_ != kInvalidSocket; }

  private:
    SocketHandle socket_{kInvalidSocket};
    std::uint64_t sent_frames_{0};
};

}  // namespace cmb::sender
