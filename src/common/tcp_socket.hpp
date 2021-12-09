#pragma once

#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "exception.hpp"

namespace spx {
static const int INVALID_SOCKET_FD = -1;

class TcpSocket {
 public:
  TcpSocket() {
    fd_ = socket(PF_INET, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET_FD) {
      throw Exception("Failed to create tcp socket");
    }
  }

  explicit TcpSocket(int fd) : fd_(fd) {}

  TcpSocket(const TcpSocket &) = delete;

  TcpSocket(TcpSocket &&other) noexcept { swap(other); }

  ~TcpSocket() {
    if (fd_ != INVALID_SOCKET_FD) {
      close(fd_);
    }
  }

  TcpSocket &operator=(const TcpSocket &) = delete;

  TcpSocket &operator=(TcpSocket &&other) noexcept {
    swap(other);
    return *this;
  }

  int getFileDescriptor() const { return fd_; }

  ssize_t send(const char *data_ptr, size_t data_len) const {
    return ::send(getFileDescriptor(), data_ptr, data_len, 0);
  }

  ssize_t send(const std::string &data) const {
    const char *data_ptr = data.c_str();
    size_t data_len = std::char_traits<char>::length(data_ptr);

    return send(data_ptr, data_len);
  }

  ssize_t receive(char *buffer, size_t buffer_len) const {
    return recv(getFileDescriptor(), buffer, buffer_len, 0);
  }

  friend std::ostream &operator<<(std::ostream &os, const TcpSocket &that) {
    os << "TcpSocket["
       << "fd=" << that.getFileDescriptor() << "]";
    return os;
  }

  bool operator==(const TcpSocket &other) const { return fd_ == other.fd_; }

 private:
  int fd_ = INVALID_SOCKET_FD;

  void swap(TcpSocket &other) noexcept { std::swap(this->fd_, other.fd_); }
};
}  // namespace spx
