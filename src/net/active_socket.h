#pragma once

#include <netdb.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../common/exception.h"
#include "tcp_socket.h"

namespace spx {

class ZeroLengthMessageException : public Exception {
 public:
  ZeroLengthMessageException() : Exception("Message with length 0") {}
};

class ActiveSocket : public TcpSocket {
 public:
  static std::unique_ptr<ActiveSocket> create(int fd);
  static std::unique_ptr<ActiveSocket> create(const addrinfo *info);

  size_t send(const void *data_ptr, size_t data_len) const;
  size_t send(const std::string &data) const;
  size_t send(const std::vector<char> &data) const;

  size_t receive(void *buffer, size_t buffer_len) const;

  void shutdownWrite();

  void connect(const sockaddr *addr, const socklen_t &addr_len);

 private:
  explicit ActiveSocket(int fd);
  explicit ActiveSocket(const addrinfo *info);
};
}  // namespace spx
