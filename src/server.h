#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cache/cache.h"
#include "net/passive_socket.h"
#include "clients_list.h"

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
  std::unique_ptr<Cache> cache_;
  ClientsList clients_;
  std::unordered_map<std::string, std::list<std::reference_wrapper<Client>>>
      request_clients_map;
  static bool is_running_;

  void acceptClient();
  void discardClient(Client& client);

  void handleServerSocketEvents(const int& events_bitmask);
  void handleClientSocketEvents(Client& client, const int& revents);

  void readRequestFromClient(Client& client);
  void sendRequestToServer(Client& client);
  void readResponseStatusCodeFromServer(Client& client);
  void readResponseFromServer(Client& client);
  void sendResponseToClient(Client& client);
  void sendUncachedResponseToClient(Client& client);
  void sendCachedResponseToClient(Client& client);

  void addServerPollfd(std::vector<pollfd>& pfds);
  std::vector<pollfd> getPollfds();
  std::vector<pollfd> poll();

  void setClientToGetResponseFromCache(Client& client);

  void addToRequestClients(Client& client);
  void removeFromRequestClients(Client& client);
  bool hasClientsWithRequest(const std::string& request);
  void prepareAllWaitingClientsForSending(const std::string& request);

  static void writeResponseChunkToClientBuffer(Client& client,
                                               const std::vector<char>& chunk);
  void writeResponseChunkToCache(Client& client,
                                 const std::vector<char>& chunk);
  void fallbackToClientBuffer(Client& original_client,
                              const std::vector<char>& chunk);
};

}  // namespace spx
