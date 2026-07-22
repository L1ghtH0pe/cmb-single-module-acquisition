#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cmb::receiver {

class TcpReceiver {
  public:
#ifdef _WIN32
    using SocketHandle = std::uintptr_t;
    static constexpr SocketHandle kInvalidSocket = ~SocketHandle{0};
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

    TcpReceiver();
    ~TcpReceiver();

    TcpReceiver(const TcpReceiver&) = delete;
    TcpReceiver& operator=(const TcpReceiver&) = delete;

    bool listen_on(std::uint16_t port);
    bool accept_one();
    bool receive_exact(std::vector<std::byte>& out, std::size_t byte_count);
    void close();

  private:
    SocketHandle listen_socket_{kInvalidSocket};
    SocketHandle client_socket_{kInvalidSocket};
};

}  // namespace cmb::receiver
