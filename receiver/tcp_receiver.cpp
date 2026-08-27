#include "receiver/tcp_receiver.h"

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
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cmb::receiver {
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

void close_socket(TcpReceiver::SocketHandle socket) {
    closesocket(static_cast<NativeSocket>(socket));
}
#else
using NativeSocket = int;

void close_socket(TcpReceiver::SocketHandle socket) {
    ::close(static_cast<NativeSocket>(socket));
}
#endif

}  // namespace

TcpReceiver::TcpReceiver() {
#ifdef _WIN32
    (void)wsa_session();
#endif
}

TcpReceiver::~TcpReceiver() {
    close();
}

bool TcpReceiver::listen_on(std::uint16_t port, const std::string& bind_host) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port);
    const char* node = bind_host.empty() || bind_host == "0.0.0.0" ? nullptr : bind_host.c_str();
    if (getaddrinfo(node, port_text.c_str(), &hints, &results) != 0) {
        return false;
    }

    bool listening = false;
    for (auto* rp = results; rp != nullptr; rp = rp->ai_next) {
        listen_socket_ = static_cast<SocketHandle>(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
        if (listen_socket_ == kInvalidSocket) {
            continue;
        }

        int reuse = 1;
        setsockopt(static_cast<NativeSocket>(listen_socket_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (::bind(static_cast<NativeSocket>(listen_socket_), rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0 &&
            ::listen(static_cast<NativeSocket>(listen_socket_), 1) == 0) {
            listening = true;
            break;
        }
        close();
    }

    freeaddrinfo(results);
    return listening;
}

bool TcpReceiver::accept_one() {
    if (listen_socket_ == kInvalidSocket) {
        return false;
    }

    client_socket_ = static_cast<SocketHandle>(::accept(static_cast<NativeSocket>(listen_socket_), nullptr, nullptr));
    return client_socket_ != kInvalidSocket;
}

bool TcpReceiver::receive_exact(std::span<std::byte> out) {
    if (client_socket_ == kInvalidSocket) {
        return false;
    }

    std::size_t received = 0;
    while (received < out.size()) {
        const auto remaining = out.size() - received;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 64 * 1024));
        const int rc = ::recv(static_cast<NativeSocket>(client_socket_), reinterpret_cast<char*>(out.data() + received), chunk, 0);
        if (rc <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(rc);
    }

    return true;
}

bool TcpReceiver::receive_exact(std::vector<std::byte>& out, std::size_t byte_count) {
    out.resize(byte_count);
    return receive_exact(std::span<std::byte>(out));
}

void TcpReceiver::close() {
    if (client_socket_ != kInvalidSocket) {
        close_socket(client_socket_);
        client_socket_ = kInvalidSocket;
    }
    if (listen_socket_ != kInvalidSocket) {
        close_socket(listen_socket_);
        listen_socket_ = kInvalidSocket;
    }
}

}  // namespace cmb::receiver
