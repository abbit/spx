#pragma once

#include <netdb.h>

#include <cstdint>
#include <iostream>
#include <memory>

#include "tcp_socket.hpp"

namespace spx {
class ConnectionType {
 public:
  enum class Enum : char {
    to_client,
    to_server,
  };

  static const char *toString(const Enum &type) {
    switch (type) {
      case Enum::to_server:
        return "to_server";
      case Enum::to_client:
        return "to_client";
    }
  }
};

class ActiveSocket : public TcpSocket {
 public:
  static std::unique_ptr<ActiveSocket> create(int fd,
                                              ConnectionType::Enum type) {
    return std::unique_ptr<ActiveSocket>(new ActiveSocket(fd, type));
  }

  static std::unique_ptr<ActiveSocket> create(const addrinfo *const info,
                                              ConnectionType::Enum type) {
    return std::unique_ptr<ActiveSocket>(new ActiveSocket(info, type));
  }

  size_t send(const void *data_ptr, size_t data_len) const {
    ssize_t res = ::send(getFileDescriptor(), data_ptr, data_len, 0);
    if (res == -1) {
      std::stringstream ss;
      ss << "Error on send(): " << std::strerror(errno);
      throw Exception(ss.str());
    }

    return static_cast<size_t>(res);
  }

  size_t send(const std::string &data) const {
    const char *data_ptr = data.c_str();
    size_t data_len = std::char_traits<char>::length(data_ptr);

    return send(data_ptr, data_len);
  }

  size_t receive(void *buffer, size_t buffer_len) const {
    ssize_t res = recv(getFileDescriptor(), buffer, buffer_len, 0);
    if (res == -1) {
      std::stringstream ss;
      ss << "Error on recv(): " << std::strerror(errno);
      throw Exception(ss.str());
    }

    return static_cast<size_t>(res);
  }

  void shutdownWrite() {
    if (shutdown(getFileDescriptor(), SHUT_WR) == -1) {
      std::stringstream ss;
      ss << "Error on shutdown(write): " << std::strerror(errno);
      throw Exception(ss.str());
    }
  }

  void connect(const sockaddr *const addr, const socklen_t &addr_len) {
    if (::connect(getFileDescriptor(), addr, addr_len) == -1) {
      throw Exception("Error on connect()");
    }
  }

  ConnectionType::Enum getType() const { return type_; }

 private:
  ConnectionType::Enum type_;

  ActiveSocket(int fd, const ConnectionType::Enum &type)
      : TcpSocket(fd), type_(type) {}

  ActiveSocket(const addrinfo *const info, const ConnectionType::Enum &type)
      : TcpSocket(info), type_(type) {}
};
}  // namespace spx
