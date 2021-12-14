#include "clients_list.h"

#include <memory>
#include <utility>

namespace spx {
std::unique_ptr<Client> Client::create() {
  return std::unique_ptr<Client>(new Client());
}

Client::Client() = default;

std::deque<std::vector<char>>& Client::getBuffer() { return buffer; }

ClientsList::ClientsList(size_t max_fds) : clients_((max_fds - 4) / 2) {}

std::vector<pollfd> ClientsList::getPollfds() {
  std::vector<pollfd> res;

  for (auto& client : clients_) {
    if (!client) continue;

    switch (client->state) {
      case Client::State::waiting_for_request:
        res.push_back(
            {client->client_connection->getFileDescriptor(), POLLIN, 0});
        break;
      case Client::State::sending_request:
        res.push_back(
            {client->server_connection->getFileDescriptor(), POLLOUT, 0});
        break;
      case Client::State::transfering_response:
        res.push_back(
            {client->server_connection->getFileDescriptor(), POLLIN, 0});
        res.push_back(
            {client->client_connection->getFileDescriptor(), POLLOUT, 0});
        break;
    }
  }

  return res;
}

Client& ClientsList::getClient(int fd) {
  for (auto& c : clients_) {
    if (c != nullptr && (c->client_connection->getFileDescriptor() == fd ||
                         (c->server_connection &&
                          c->server_connection->getFileDescriptor() == fd))) {
      return *c;
    }
  }
  return *clients_[fd];
}

void ClientsList::acceptConnection(
    std::unique_ptr<ActiveSocket>&& client_conn) {
  int client_fd = client_conn->getFileDescriptor();
  clients_[client_fd] = Client::create();
  clients_[client_fd]->client_connection = std::move(client_conn);
}

// TODO: improve performance
void ClientsList::closeConnection(const ActiveSocket& connection) {
  if (connection.isValid()) {
    for (auto& c : clients_) {
      if (c != nullptr &&
          (*c->client_connection == connection ||
           (c->server_connection && *c->server_connection == connection))) {
        c = nullptr;
      }
    }
  }
}

size_t ClientsList::size() {
  size_t res = 0;
  for (auto& e : clients_) {
    if (e) ++res;
  }
  return res;
}
}  // namespace spx
