#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "clients_list.h"
#include "common/passive_socket.h"

namespace spx {
class Server {
 public:
  Server(uint16_t port, rlim_t max_fds);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  void start();
  static void stop(int);

 private:
  std::unique_ptr<PassiveSocket> server_socket_;
  ClientsList clients_;
  static bool is_running_;

  void acceptConnection();
  void closeConnection(const ActiveSocket& connection);

  void handleServerSocketEvents(const int& events_bitmask);
  void handleClientSocketEvents(Client& client, const int& revents);

  void readRequestFromClient(Client& client);
  void sendRequestToServer(Client& client);
  void readResponseFromServer(Client& client);
  void sendResponseToClient(Client& client);

  void addServerPollfd(std::vector<pollfd>& pfds);
  std::vector<pollfd> getPollfds();
  std::vector<pollfd> poll();
};
}  // namespace spx
