#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>

#include "exception.hpp"

namespace spx {
const int INVALID_SOCKET_FD = -1;

class TcpSocket {
 public:
  bool operator==(const TcpSocket &other) const {
    return getFileDescriptor() == other.getFileDescriptor();
  }

  bool operator==(const int &other) const {
    return getFileDescriptor() == other;
  }

  friend std::ostream &operator<<(std::ostream &os, const TcpSocket &that) {
    os << "TcpSocket("
       << "fd=" << that.getFileDescriptor() << ")";
    return os;
  }

  bool isValid() const { return getFileDescriptor() != INVALID_SOCKET_FD; }

  int getFileDescriptor() const { return fd_; }

 protected:
  TcpSocket(int domain, int type, int protocol) {
    fd_ = socket(domain, type, protocol);
    if (fd_ == INVALID_SOCKET_FD) {
      throw Exception("Failed to create tcp socket");
    }
    std::cout << "Created socket with fd " << fd_ << std::endl;
  }

  explicit TcpSocket(const addrinfo *const info)
      : TcpSocket(info->ai_family, info->ai_socktype, info->ai_protocol) {}

  TcpSocket() : TcpSocket(PF_INET, SOCK_STREAM, 0) {}

  explicit TcpSocket(int fd) : fd_(fd) {
    std::cout << "Created socket with fd " << fd_ << std::endl;
  }

  TcpSocket(const TcpSocket &) = delete;

  TcpSocket(TcpSocket &&other) noexcept { swap(other); }

  ~TcpSocket() {
    if (isValid()) {
      int fd = getFileDescriptor();
      close(fd);
      std::cout << "Closed socket with fd " << fd << std::endl;
    }
  }

  TcpSocket &operator=(const TcpSocket &) = delete;

  TcpSocket &operator=(TcpSocket &&other) noexcept {
    swap(other);
    return *this;
  }

 private:
  int fd_{INVALID_SOCKET_FD};

  void swap(TcpSocket &other) noexcept { std::swap(this->fd_, other.fd_); }
};
}  // namespace spx
