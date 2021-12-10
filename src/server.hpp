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
  struct Client {
    std::unique_ptr<TcpSocket> client_connection{nullptr};
    std::unique_ptr<TcpSocket> server_connection{nullptr};
    std::unique_ptr<char[]> buffer{new char[16 * BUFSIZ]};
  };

  Server(uint16_t port, rlim_t max_fds);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  void start();

 private:
  ServerSocket server_socket_;
  std::vector<pollfd> pollfds_;
  std::vector<Client> clients_;

  inline std::vector<pollfd>::iterator getPollfdsServerIter() {
    return pollfds_.begin();
  }

  inline std::vector<pollfd>::iterator getPollfdsClientsBeginIter() {
    return ++pollfds_.begin();
  }

  void refreshPollfds();
  void acceptConnection();
  void handleClientRequest(const TcpSocket& client_connection);
  void closeConnection(const TcpSocket& connection);
  void handleServerSocketEvents(const short& events_bitmask);
  void handleClientSocketEvents(const TcpSocket& socket,
                                const short& events_bitmask);
};
}  // namespace spx
