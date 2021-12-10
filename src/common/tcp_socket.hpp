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
  TcpSocket(int domain, int type, int protocol) {
    fd_ = socket(domain, type, protocol);
    if (fd_ == INVALID_SOCKET_FD) {
      throw Exception("Failed to create tcp socket");
    }
    std::cout << "Created socket with fd " << fd_ << std::endl;
  }

  TcpSocket() : TcpSocket(PF_INET, SOCK_STREAM, 0) {}

  explicit TcpSocket(int fd) : fd_(fd) {
    std::cout << "Created socket with fd " << fd_ << std::endl;
  }

  explicit TcpSocket(const addrinfo *const info)
      : TcpSocket(info->ai_family, info->ai_socktype, info->ai_protocol) {}

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

  int getFileDescriptor() const { return fd_; }

  bool isValid() const { return getFileDescriptor() != INVALID_SOCKET_FD; }

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

  friend std::ostream &operator<<(std::ostream &os, const TcpSocket &that) {
    os << "TcpSocket("
       << "fd=" << that.getFileDescriptor() << ")";
    return os;
  }

  bool operator==(const TcpSocket &other) const {
    return getFileDescriptor() == other.getFileDescriptor();
  }

  bool operator==(const int &other) const {
    return getFileDescriptor() == other;
  }

 private:
  int fd_{INVALID_SOCKET_FD};

  void swap(TcpSocket &other) noexcept { std::swap(this->fd_, other.fd_); }
};

class TcpSocketFactory {
 public:
  static std::unique_ptr<TcpSocket> newTcpSocket(int fd) {
    return std::make_unique<TcpSocket>(fd);
  }

  static std::unique_ptr<TcpSocket> newTcpSocket(const addrinfo *const info) {
    return std::make_unique<TcpSocket>(info);
  }

 private:
};
}  // namespace spx
