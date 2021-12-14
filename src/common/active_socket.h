#pragma once

#include <netdb.h>

#include <cstdint>
#include <memory>
#include <string>

#include "tcp_socket.h"

namespace spx {
class ConnectionType {
 public:
  enum class Enum : char {
    to_client,
    to_server,
  };

  static const char *toString(const Enum &type);
};

class ActiveSocket : public TcpSocket {
 public:
  static std::unique_ptr<ActiveSocket> create(int fd,
                                              ConnectionType::Enum type);
  static std::unique_ptr<ActiveSocket> create(const addrinfo *info,
                                              ConnectionType::Enum type);

  size_t send(const void *data_ptr, size_t data_len) const;
  size_t send(const std::string &data) const;

  size_t receive(void *buffer, size_t buffer_len) const;

  void shutdownWrite();

  void connect(const sockaddr *addr, const socklen_t &addr_len);

  ConnectionType::Enum getType() const;

 private:
  ConnectionType::Enum type_;

  ActiveSocket(int fd, const ConnectionType::Enum &type);
  ActiveSocket(const addrinfo *info, const ConnectionType::Enum &type);
};
}  // namespace spx
