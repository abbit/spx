#include "server.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "common/exception.h"
#include "common/http_request.h"
#include "common/passive_socket.h"

namespace spx {
namespace http = httpparser;

namespace {
const int TIMEOUT = 600000;  // in ms
const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
const int MAX_REQUEST_SIZE = 16 * 1024;  // 16 kb
}  // namespace

bool Server::is_running_ = false;

void Server::stop(int) { Server::is_running_ = false; }

Server::Server(uint16_t port, rlim_t max_fds) : clients_(max_fds) {
  server_socket_ = PassiveSocket::create(port);

  signal(SIGTERM, Server::stop);
  signal(SIGINT, Server::stop);
}

Server::~Server() { std::cout << "Server destructor called" << std::endl; }

void Server::addServerPollfd(std::vector<pollfd> &pfds) {
  pfds.push_back({server_socket_->getFileDescriptor(), POLLIN, 0});
}

std::vector<pollfd> Server::getPollfds() {
  std::vector<pollfd> pfds = clients_.getPollfds();
  addServerPollfd(pfds);
  return pfds;
}

std::vector<pollfd> Server::poll() {
  std::vector<pollfd> pfds = getPollfds();

  int poll_result = ::poll(pfds.data(), pfds.size(), TIMEOUT);
  std::cout << "======================\n"
            << "clients=" << clients_.size() << ", polled=" << poll_result
            << "\n======================" << std::endl;

  if (poll_result < 0) {
    throw Exception("error on poll()");
  }

  if (poll_result == 0) {
    std::cout << "poll() timed out" << std::endl;
    return {};
  }

  pfds.erase(std::remove_if(pfds.begin(), pfds.end(),
                            [](const pollfd &pfd) { return pfd.revents == 0; }),
             pfds.end());

  return pfds;
}

void Server::start() {
  server_socket_->listen(PENDING_CONNECTIONS_QUEUE_LEN);
  std::cout << "Listening on port " << server_socket_->getPort() << std::endl;

  is_running_ = true;
  while (is_running_) {
    std::vector<pollfd> pfds = poll();

    for (const auto &pfd : pfds) {
      if (*server_socket_ == pfd.fd) {
        handleServerSocketEvents(pfd.revents);
      } else {
        handleClientSocketEvents(clients_.getClient(pfd.fd), pfd.revents);
      }
    }
  }
}

void Server::handleServerSocketEvents(const int &events_bitmask) {
  if ((events_bitmask & (POLLERR | POLLNVAL)) > 0) {
    throw Exception("Error on server socket");
  } else if ((events_bitmask & POLLIN) > 0) {
    try {
      acceptConnection();
    } catch (const Exception &e) {
      std::cerr << "Error on accepting connection to server: " << e.what()
                << std::endl;
    }
  }
}

void Server::handleClientSocketEvents(Client &client, const int &revents) {
  try {
    if ((revents & POLLERR) > 0) {
      closeConnection(*client.client_connection);
      return;
    }

    switch (client.state) {
      case Client::State::waiting_for_request:
        readRequestFromClient(client);
        break;
      case Client::State::sending_request:
        sendRequestToServer(client);
        break;
      case Client::State::transfering_response:
        if (revents == POLLHUP) {
          closeConnection(*client.client_connection);
        } else if ((revents & POLLIN) > 0) {
          readResponseFromServer(client);
        } else if ((revents & POLLOUT) > 0) {
          sendResponseToClient(client);
        } else {
          std::stringstream ss;
          ss << "Unexpected revent " << revents
             << " with \"sending_response\" client state)";
          throw Exception(ss.str());
        }
        break;
    }
  } catch (const Exception &e) {
    std::cerr << "Error while handling client socket events: " << e.what()
              << std::endl;
  }
}

void Server::acceptConnection() {
  clients_.acceptConnection(server_socket_->accept());
  std::cout << "Connection accepted" << std::endl;
}

void Server::closeConnection(const ActiveSocket &connection) {
  clients_.closeConnection(connection);
  std::cout << "Connection closed" << std::endl;
}

std::unique_ptr<ActiveSocket> connectToHost(const std::string &hostname,
                                            const std::string &port) {
  // get addrinfo by hostname
  addrinfo hints{};
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *servinfo;
  if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &servinfo) != 0) {
    throw Exception("error on getaddrinfo()");
  }

  // connect to host
  std::unique_ptr<ActiveSocket> res = nullptr;
  for (addrinfo *p = servinfo; p != nullptr; p = p->ai_next) {
    try {
      std::unique_ptr<ActiveSocket> socket =
          ActiveSocket::create(p, ConnectionType::Enum::to_server);
      socket->connect(p->ai_addr, p->ai_addrlen);

      auto *in = reinterpret_cast<sockaddr_in *>(p->ai_addr);
      std::cout << "Connected to " << hostname << "(" << inet_ntoa(in->sin_addr)
                << ")"
                << ", port " << ntohs(in->sin_port) << std::endl;

      res = std::move(socket);
      break;
    } catch (const Exception &e) {
    }
  }
  freeaddrinfo(servinfo);

  if (!res || !res->isValid()) {
    std::stringstream ss;
    ss << "Can't connect to host " << hostname << ":" << port;
    throw Exception(ss.str());
  }

  return res;
}

std::string readHttpRequest(const ActiveSocket &connection) {
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

void Server::readRequestFromClient(Client &client) {
  std::cout << "Read request from client " << std::endl;

  const ActiveSocket &client_conn = *client.client_connection;

  try {
    std::string received_request_str = readHttpRequest(client_conn);

    std::cout << "[" << client_conn << "] "
              << "Request received:\n"
              << received_request_str << std::endl;

    client.request = HttpRequest(received_request_str);

    client.server_connection =
        connectToHost(client.request.url.hostname(),
                      std::to_string(client.request.url.httpPort()));

    client.state = Client::State::sending_request;
  } catch (const Exception &e) {
    std::cerr << "Error while reading request from client: " << e.what()
              << std::endl;
    closeConnection(client_conn);
  }
}

void Server::sendRequestToServer(Client &client) {
  std::cout << "Send request to server" << std::endl;

  const ActiveSocket &client_conn = *client.client_connection;

  try {
    ActiveSocket &server_conn = *client.server_connection;
    const HttpRequest &request = client.request;

    server_conn.send(request.toString());
    server_conn.shutdownWrite();

    client.state = Client::State::transfering_response;

    std::cout << "[" << client_conn << "] "
              << "Request sent:\n"
              << request.toString() << std::endl;
  } catch (const Exception &e) {
    std::cerr << "Error while sending request to server: " << e.what()
              << std::endl;
    closeConnection(client_conn);
  }
}

void Server::readResponseFromServer(Client &client) {
  if (client.is_done_reading_response) return;
  std::cout << "Read response from server" << std::endl;

  const ActiveSocket &client_conn = *client.client_connection;

  try {
    ActiveSocket &server_conn = *client.server_connection;

    char buffer[BUFSIZ];
    size_t buffer_size = sizeof(buffer);
    size_t message_size = server_conn.receive(buffer, buffer_size);

    if (message_size == 0) {
      client.is_done_reading_response = true;
      return;
    }

    std::deque<std::vector<char>> &client_buf = client.getBuffer();
    client_buf.emplace_back(buffer, buffer + message_size);

    std::cout << "[" << client_conn << "] "
              << "Response read (size=" << message_size << "):\n"
              << buffer << std::endl;
  } catch (const Exception &e) {
    std::cerr << "Error while reading response from server: " << e.what()
              << std::endl;
    closeConnection(client_conn);
  }
}

void Server::sendResponseToClient(Client &client) {
  std::cout << "Send response to client" << std::endl;

  const ActiveSocket &client_conn = *client.client_connection;

  try {
    std::deque<std::vector<char>> &buffer = client.getBuffer();
    if (buffer.empty()) {
      if (client.is_done_reading_response) {
        std::cout << "DONE!!!" << std::endl;
        closeConnection(client_conn);
        return;
      }

      std::cout << "No data" << std::endl;
      return;
    }

    std::vector<char> chunk = buffer.front();
    size_t written_size = client_conn.send(chunk.data(), chunk.size());
    buffer.pop_front();

    if (written_size != chunk.size()) {
      throw Exception("partial write");
    }

    std::cout << "[" << client_conn << "] "
              << "Response sent: \n"
              << chunk.data() << std::endl;
  } catch (const Exception &e) {
    std::cerr << "Error while sending response to client: " << e.what()
              << std::endl;
    closeConnection(client_conn);
  }
}
}  // namespace spx
