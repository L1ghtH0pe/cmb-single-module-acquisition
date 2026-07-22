#include "sender/tcp_sender.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cmb::sender {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;

struct WsaSession {
    WsaSession() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WsaSession() { WSACleanup(); }
};

WsaSession& wsa_session() {
    static WsaSession session;
    return session;
}

void close_socket(TcpSender::SocketHandle socket) {
    closesocket(static_cast<NativeSocket>(socket));
}
#else
using NativeSocket = int;

void close_socket(TcpSender::SocketHandle socket) {
    ::close(static_cast<NativeSocket>(socket));
}
#endif

}  // namespace

TcpSender::TcpSender() {
#ifdef _WIN32
    (void)wsa_session();
#endif
}

TcpSender::~TcpSender() {
    close();
}

bool TcpSender::connect_to(const std::string& host, std::uint16_t port) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &results) != 0) {
        return false;
    }

    bool connected = false;
    for (auto* rp = results; rp != nullptr; rp = rp->ai_next) {
        socket_ = static_cast<SocketHandle>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
        if (socket_ == kInvalidSocket) {
            continue;
        }

        int flag = 1;
        setsockopt(static_cast<NativeSocket>(socket_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        if (::connect(static_cast<NativeSocket>(socket_), rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            connected = true;
            break;
        }
        close();
    }

    freeaddrinfo(results);
    return connected;
}

bool TcpSender::send(const std::vector<std::byte>& bytes) {
    if (socket_ == kInvalidSocket) {
        return false;
    }

    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto remaining = bytes.size() - sent;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 64 * 1024));
        const int rc = ::send(static_cast<NativeSocket>(socket_), reinterpret_cast<const char*>(bytes.data() + sent), chunk, 0);
        if (rc <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(rc);
    }

    ++sent_frames_;
    return true;
}

void TcpSender::close() {
    if (socket_ != kInvalidSocket) {
        close_socket(socket_);
        socket_ = kInvalidSocket;
    }
}

}  // namespace cmb::sender
