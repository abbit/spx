#pragma once

#include <netdb.h>

#include <cstdint>
#include <iostream>
#include <memory>

#include "tcp_socket.hpp"

namespace spx {
class ServerSocket : public TcpSocket {
 public:
  explicit ServerSocket(uint16_t port) : TcpSocket(), port_(port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(nullptr, std::to_string(port_).c_str(), &hints, &res) !=
        0) {
      throw Exception("Failed to get addrinfo for server socket");
    }

    if (bind(getFileDescriptor(), res->ai_addr, res->ai_addrlen) != 0) {
      throw Exception("Failed to bind server socket");
    }

    freeaddrinfo(res);
  }

  void listen(int backlog) {
    if (::listen(getFileDescriptor(), backlog) != 0) {
      throw Exception("error on listen");
    }
  }

  TcpSocket accept() {
    int conn_fd = ::accept(getFileDescriptor(), nullptr, nullptr);
    if (conn_fd == INVALID_SOCKET_FD) {
      throw Exception("error on accepting connection");
    }

    return TcpSocket(conn_fd);
  }

  uint16_t getPort() { return port_; }

 private:
  uint16_t port_;
};
}  // namespace spx
