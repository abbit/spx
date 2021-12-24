#include "clients_list.h"

#include <memory>
#include <utility>

namespace spx {
namespace {
const int MAX_REQUEST_SIZE = 16 * 1024;  // 16 kb

inline size_t calculateMaximumClients(size_t max_fds) {
  return (max_fds - 4) / 2;
}

std::string readHttpRequest(const ActiveSocket& connection) {
  char request_raw[MAX_REQUEST_SIZE];
  char buffer[BUFSIZ];
  size_t message_size;
  size_t written = 0;
  while ((message_size = connection.receive(buffer, sizeof(buffer))) > 0) {
    if (written + message_size > sizeof(request_raw)) {
      throw Exception("Too large request");
    }

    strncpy(request_raw + written, buffer, message_size);
    written += message_size;

    if (strstr(request_raw, "\r\n\r\n") != nullptr) break;
  }

  return {request_raw, static_cast<std::string::size_type>(written)};
}
}  // namespace

std::unique_ptr<Client> Client::create(
    std::unique_ptr<ActiveSocket> client_conn) {
  auto client = std::unique_ptr<Client>(new Client());
  client->client_connection = std::move(client_conn);
  return client;
}

Client::Client() = default;

bool Client::operator==(const Client& other) const {
  return client_connection == other.client_connection;
}

bool Client::operator!=(const Client& other) const { return !(*this == other); }

std::ostream& operator<<(std::ostream& os, const Client& that) {
  os << "[Client("
     << "fd=" << that.client_connection->getFileDescriptor() << ")] ";
  return os;
}

size_t Client::sendToClient(const std::vector<char>& chunk) {
  size_t written_size = client_connection->send(chunk);
  sent_bytes += written_size;
  return written_size;
}

void Client::sendToClientFromChunks() {
  std::vector<char> chunk = chunks.front();
  chunks.pop_front();
  size_t written_size = sendToClient(chunk);
  if (written_size != chunk.size()) {
    std::cout << "partial write" << std::endl;
    chunks.emplace_front(chunk.data() + written_size,
                         chunk.data() + chunk.size());
  }
}
void Client::getHttpRequest() {
  std::string received_request_str = readHttpRequest(*client_connection);
  request = HttpRequest(received_request_str);
  request_str = request.toString();
}

bool Client::isReadingFromCache() const {
  return state == Client::State::got_response && should_read_from_cache &&
         server_connection == nullptr;
}

void Client::obtainServerConnection(Client& other) {
  server_connection = std::move(other.server_connection);
  state = Client::State::waiting_response;
  should_write_to_cache = true;
}

ClientsList::ClientsList(size_t max_fds)
    : clients_(calculateMaximumClients(max_fds)) {}

std::vector<pollfd> ClientsList::getPollfds() {
  std::vector<pollfd> res;

  for (auto& client : clients_) {
    if (!client) continue;

    int client_fd = client->client_connection->getFileDescriptor();
    int server_fd = -1;
    if (client->server_connection) {
      server_fd = client->server_connection->getFileDescriptor();
    }

    switch (client->state) {
      case Client::State::waiting_for_request:
        res.push_back({client_fd, POLLIN, 0});
        break;
      case Client::State::waiting_for_response_for_another_client:
        break;
      case Client::State::sending_request:
        assert(server_fd != -1);
        res.push_back({server_fd, POLLOUT, 0});
        break;
      case Client::State::waiting_response_status_code:
        assert(server_fd != -1);
        res.push_back({server_fd, POLLIN, 0});
        break;
      case Client::State::waiting_response:
        assert(server_fd != -1);
        res.push_back({server_fd, POLLIN, 0});
        res.push_back({client_fd, POLLOUT, 0});
        break;
      case Client::State::got_response:
      case Client::State::getting_response_from_another_client:
        res.push_back({client_fd, POLLOUT, 0});
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

void ClientsList::acceptClient(std::unique_ptr<ActiveSocket> client_conn) {
  int fd = client_conn->getFileDescriptor();
  clients_[fd] = Client::create(std::move(client_conn));
  std::cout << "Client(fd=" << fd << ") accepted" << std::endl;
}

void ClientsList::discardClient(Client& client) {
  int fd = client.client_connection->getFileDescriptor();
  clients_[fd] = nullptr;
  std::cout << "Client(fd=" << fd << ") discarded" << std::endl;
}

}  // namespace spx
