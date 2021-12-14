#pragma once

#include <poll.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "clients_list.hpp"
#include "common/passive_socket.hpp"

namespace spx {
class Server {
 public:
  Server(uint16_t port, rlim_t max_fds);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  void start();
  static void stop(int _ignored);

 private:
  std::unique_ptr<PassiveSocket> server_socket_;
  ClientsList clients_;
  static bool is_running_;

  void acceptConnection();
  void closeConnection(const ActiveSocket& connection);

  void handleServerSocketEvents(const short& events_bitmask);
  void handleClientSocketEvents(Client& client, const short& revents);

  void readRequestFromClient(Client& client);
  void sendRequestToServer(Client& client);
  void readResponseFromServer(Client& client);
  void sendResponseToClient(Client& client);

  void addServerPollfd(std::vector<pollfd>& pfds);
  std::vector<pollfd> getPollfds();
  std::vector<pollfd> poll();
};
}  // namespace spx
