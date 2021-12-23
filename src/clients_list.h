#pragma once

#include <sys/poll.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "common/active_socket.h"
#include "common/http_request.h"

namespace spx {

class Client {
 public:
  enum class State : char {
    waiting_for_request,  // client: POLLIN, no server connection yet
    waiting_for_response_for_another_client,  // client: 0, no server connection
                                              // yet
    sending_request,                          // client: 0, server: POLLOUT
    waiting_response_status_code,             // client: 0, server: POLLIN
    waiting_response,                         // client: POLLOUT, server: POLLIN
    got_response,                             // client: POLLOUT, server: 0
  };

  std::string request_str;
  HttpRequest request;
  std::unique_ptr<ActiveSocket> client_connection{nullptr};
  std::unique_ptr<ActiveSocket> server_connection{nullptr};
  State state{State::waiting_for_request};
  size_t sent_bytes{0};
  std::deque<std::vector<char>> chunks;
  bool should_write_to_cache{false};
  bool should_read_from_cache{false};

  static std::unique_ptr<Client> create(
      std::unique_ptr<ActiveSocket> client_conn);

  bool operator==(const Client& other) const;
  bool operator!=(const Client& other) const;

  friend std::ostream& operator<<(std::ostream& os, const Client& that);

  void getHttpRequest();

  bool isReadingFromCache() const;
  void obtainServerConnection(Client &other);

  size_t sendToClient(const std::vector<char>& chunk);
  void sendToClientFromChunks();

 private:
  Client();
};

class ClientsList {
 public:
  explicit ClientsList(size_t max_clients);

  Client& getClient(int fd);
  std::vector<pollfd> getPollfds();

  void acceptClient(std::unique_ptr<ActiveSocket> client_conn);
  void discardClient(Client& client);

 private:
  std::vector<std::unique_ptr<Client>> clients_;
};

}  // namespace spx
