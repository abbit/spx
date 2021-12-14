#pragma once

#include <sys/poll.h>

#include <deque>
#include <memory>
#include <vector>

#include "common/active_socket.h"
#include "common/http_request.h"

namespace spx {
class Client {
 public:
  enum class State : char {
    waiting_for_request,   // client: POLLIN, no server
    sending_request,       // client: 0, server: POLLOUT
    transfering_response,  // client: POLLOUT, server: POLLIN
  };

  std::unique_ptr<ActiveSocket> client_connection{nullptr};
  std::unique_ptr<ActiveSocket> server_connection{nullptr};
  State state{State::waiting_for_request};
  HttpRequest request;
  bool is_done_reading_response{false};

  static std::unique_ptr<Client> create();

  std::deque<std::vector<char>>& getBuffer();

 private:
  std::deque<std::vector<char>> buffer;

  Client();
};

class ClientsList {
 public:
  explicit ClientsList(size_t max_clients);

  Client& getClient(int fd);
  std::vector<pollfd> getPollfds();
  size_t size();

  void acceptConnection(std::unique_ptr<ActiveSocket>&& client_conn);
  void closeConnection(const ActiveSocket& connection);

 private:
  std::vector<std::unique_ptr<Client>> clients_;
};
}  // namespace spx
