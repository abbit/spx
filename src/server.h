#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "client.h"
#include "common/cache.h"
#include "common/passive_socket.h"

namespace spx {

// struct RequestClientsMapEntry {
//   std::list<std::reference_wrapper<Client>> list;
//   std::unique_ptr<Mutex> mutex;
//   std::unique_ptr<CondVar> cond_var;
// };

class Server {
 public:
  explicit Server(uint16_t port);
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  void start();
  static void stop(int);

 private:
  std::unique_ptr<PassiveSocket> server_socket_;
  //  static std::unique_ptr<Cache> cache_;
  //  static std::unordered_map<std::string, RequestClientsMapEntry>
  //      request_clients_map;
  //    static Mutex request_clients_map_mutex_;
  static bool is_running_;

  void acceptClient();
  static void *handleClient(void *arg);
  static void discardClient(Client *client);

  static void readRequestFromClient(Client &client);
  static void readResponseStatusCodeFromServer(Client &client);
  static void prepareForSendingRequest(Client &client);
  static std::vector<char> readResponseChunk(Client &client);
  static void sendRequestToServer(Client &client);
  static void transferResponse(Client &client);

  //  static void sendUncachedResponseToClient(Client &client);

  //  static bool hasClientsWithRequest(const std::string &request);
  //  static void addToRequestClients(Client &client);
  //  static void removeFromRequestClients(Client &client);
  //  static void prepareAllWaitingClientsForSending(const std::string
  //  &request);
  //  static void fallbackToClientBuffer(Client &client);
  //  static void writeResponseChunkToCache(Client &client,
  //                                        const std::vector<char> &chunk);
  //  static void setClientToGetResponseFromCache(Client &client);
  //  static void sendCachedResponseToClient(Client &client);

  //  static RequestClientsMapEntry &getRequestClientsMapEntry(
  //      const std::string &request);
};

}  // namespace spx
