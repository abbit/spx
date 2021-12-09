#pragma once

#include <poll.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/server_socket.hpp"

namespace spx {

class Server {
 public:
  Server(uint16_t port, rlim_t max_fds);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  void start();

 private:
  ServerSocket server_socket_;
  std::vector<pollfd> pollfds_;
  std::map<int, TcpSocket> clients_;

  void refreshRevents();
  void acceptConnection();
  void handleConnection(const TcpSocket& connection);
  void closeConnection(const TcpSocket& connection);
};
}  // namespace spx
