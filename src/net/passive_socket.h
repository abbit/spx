#pragma once

#include <cstdint>
#include <memory>

#include "tcp_socket.h"
#include "active_socket.h"


namespace spx {
class PassiveSocket : public TcpSocket {
 public:
  static std::unique_ptr<PassiveSocket> create(uint16_t port);

  void listen(int backlog);

  std::unique_ptr<ActiveSocket> accept();

  uint16_t getPort() const;

 private:
  uint16_t port_;

  explicit PassiveSocket(uint16_t port);
};
}  // namespace spx
