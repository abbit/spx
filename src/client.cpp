#include "client.h"

#include <memory>
#include <utility>

namespace spx {

namespace {
const int MAX_REQUEST_SIZE = 16 * 1024;  // 16 kb

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

Client* Client::create(std::unique_ptr<ActiveSocket> client_conn) {
  auto client = new Client();
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

void Client::sendChunkToClient(const std::vector<char>& chunk) {
  std::cout << *this << "Send chunk to client" << std::endl;
  size_t written_size = client_connection->send(chunk);
  sent_bytes += written_size;
  if (written_size != chunk.size()) {
    std::cout << "partial write" << std::endl;
    sendChunkToClient(
        {chunk.data() + written_size, chunk.data() + chunk.size()});
  }
}

void Client::getHttpRequest() {
  std::string received_request_str = readHttpRequest(*client_connection);
  request = HttpRequest(received_request_str);
  request_str = request.toString();
}

bool Client::isReadingFromCache() const {
  return state == Client::State::get_from_cache && should_use_cache &&
         server_connection == nullptr;
}

void Client::obtainServerConnection(Client& other) {
  server_connection = std::move(other.server_connection);
  state = Client::State::transferring_response;
}

}  // namespace spx
