#include "active_socket.h"

#include <netdb.h>

#include <iostream>
#include <memory>
#include <string>

#include "tcp_socket.h"

namespace spx {
ActiveSocket::ActiveSocket(int fd) : TcpSocket(fd) {}

ActiveSocket::ActiveSocket(const addrinfo *const info) : TcpSocket(info) {}

std::unique_ptr<ActiveSocket> ActiveSocket::create(int fd) {
  return std::unique_ptr<ActiveSocket>(new ActiveSocket(fd));
}

std::unique_ptr<ActiveSocket> ActiveSocket::create(const addrinfo *const info) {
  return std::unique_ptr<ActiveSocket>(new ActiveSocket(info));
}

size_t ActiveSocket::send(const void *data_ptr, size_t data_len) const {
  ssize_t res = ::send(getFileDescriptor(), data_ptr, data_len, 0);
  if (res == -1) {
    std::stringstream ss;
    ss << "Error on send(): " << std::strerror(errno);
    throw Exception(ss.str());
  }

  return static_cast<size_t>(res);
}

size_t ActiveSocket::send(const std::string &data) const {
  const char *data_ptr = data.c_str();
  size_t data_len = std::char_traits<char>::length(data_ptr);

  return send(data_ptr, data_len);
}

size_t ActiveSocket::send(const std::vector<char> &data) const {
  return send(data.data(), data.size());
}

size_t ActiveSocket::receive(void *buffer, size_t buffer_len) const {
  ssize_t res = recv(getFileDescriptor(), buffer, buffer_len, 0);
  if (res == -1) {
    std::stringstream ss;
    ss << "Error on recv(): " << std::strerror(errno);
    throw Exception(ss.str());
  }

  if (res == 0) {
    throw ZeroLengthMessageException();
  }

  return static_cast<size_t>(res);
}

void ActiveSocket::shutdownWrite() {
  if (shutdown(getFileDescriptor(), SHUT_WR) == -1) {
    std::stringstream ss;
    ss << "Error on shutdown(write): " << std::strerror(errno);
    throw Exception(ss.str());
  }
}

void ActiveSocket::connect(const sockaddr *const addr,
                           const socklen_t &addr_len) {
  if (::connect(getFileDescriptor(), addr, addr_len) == -1) {
    throw Exception("Error on connect()");
  }
}

}  // namespace spx
