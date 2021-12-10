#include "server.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

#include "common/exception.hpp"
#include "common/http_request.hpp"
#include "common/server_socket.hpp"

namespace spx {
namespace http = httpparser;

const int TIMEOUT = 600000;  // in ms
const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
const int MAX_REQUEST_SIZE = 16 * 1024;  // 16 kb

Server::Server(uint16_t port, rlim_t max_fds)
    : server_socket_(port), pollfds_(max_fds), clients_((max_fds - 4) / 2) {
  *getPollfdsServerIter() = {server_socket_.getFileDescriptor(), POLLIN, 0};
}

Server::~Server() { std::cout << "Server destructor called" << std::endl; }

void Server::start() {
  server_socket_.listen(PENDING_CONNECTIONS_QUEUE_LEN);
  std::cout << "Listening on port " << server_socket_.getPort() << std::endl;

  for (;;) {
    // setup pollfds
    refreshPollfds();
    int poll_result = poll(pollfds_.data(), pollfds_.size(), TIMEOUT);

    if (poll_result < 0) {
      throw Exception("error on poll()");
    }

    if (poll_result == 0) {
      std::cout << "poll() timed out" << std::endl;
      continue;
    }

    for (const auto &pfd : pollfds_) {
      if (server_socket_ == pfd.fd) {
        handleServerSocketEvents(pfd.revents);
      } else {
        handleClientSocketEvents(*clients_[pfd.fd].client_connection,
                                 pfd.revents);
      }
    }
  }
}

void Server::handleServerSocketEvents(const short &events_bitmask) {
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

void Server::handleClientSocketEvents(const TcpSocket &socket,
                                      const short &events_bitmask) {
  if ((events_bitmask & (POLLHUP | POLLERR)) > 0) {
    closeConnection(socket);
  } else if ((events_bitmask & POLLIN) > 0) {
    try {
      handleClientRequest(socket);
    } catch (const Exception &e) {
      std::cerr << e.what() << std::endl;
    }
  }
}

void Server::acceptConnection() {
  std::unique_ptr<TcpSocket> conn = server_socket_.accept();
  int id = conn->getFileDescriptor();
  clients_[id].client_connection = std::move(conn);

  std::cout << "\nConnection accepted " << *clients_[id].client_connection
            << std::endl
            << std::endl;
}

void Server::closeConnection(const TcpSocket &connection) {
  if (connection.isValid()) {
    int id = connection.getFileDescriptor();
    clients_[id].client_connection = nullptr;
    std::cout << "Connection closed " << id << std::endl;
  }
}

std::unique_ptr<TcpSocket> connectToHost(const std::string &hostname,
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
  std::unique_ptr<TcpSocket> res;
  for (addrinfo *p = servinfo; p != nullptr; p = p->ai_next) {
    try {
      std::unique_ptr<TcpSocket> socket = TcpSocketFactory::newTcpSocket(p);
      socket->connect(p->ai_addr, p->ai_addrlen);

      auto *in = (sockaddr_in *)p->ai_addr;
      std::cout << "Connected to " << hostname << "(" << inet_ntoa(in->sin_addr)
                << ")"
                << ", port " << ntohs(in->sin_port) << std::endl;

      res = std::move(socket);
      break;
    } catch (const Exception &e) {
    }
  }
  freeaddrinfo(servinfo);

  if (!res->isValid()) {
    std::stringstream ss;
    ss << "Can't connect to host " << hostname << ":" << port;
    throw Exception(ss.str());
  }

  return res;
}

std::string readHttpRequest(const TcpSocket &connection) {
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

void transferData(const TcpSocket &from, const TcpSocket &to) {
  char buffer[BUFSIZ];
  size_t buffer_size = sizeof(buffer);
  size_t message_size;
  size_t written_size;
  // TODO: blocking, fix
  while ((message_size = from.receive(buffer, buffer_size)) > 0) {
    written_size = to.send(buffer, message_size);

    if (written_size != message_size) {
      throw Exception("partial write");
      // TODO: close conn or not???
    }
  }
}

void Server::handleClientRequest(const TcpSocket &client_connection) {
  try {
    std::string received_request_str = readHttpRequest(client_connection);

    std::cout << "[" << client_connection << "] "
              << "Request received:\n"
              << received_request_str << std::endl;

    HttpRequest request(received_request_str);

    std::unique_ptr<TcpSocket> request_dest = connectToHost(
        request.url.hostname(), std::to_string(request.url.httpPort()));

    // TODO: save dest connection

    request_dest->send(request.toString());
    request_dest->shutdownWrite();

    std::cout << "[" << client_connection << "] "
              << "Request sent:\n"
              << request.toString() << std::endl;

    // send response back to requester
    transferData(*request_dest, client_connection);

    std::cout << "[" << client_connection << "] "
              << "Response sent\n"
              << std::endl;
  } catch (const Exception &e) {
    std::cerr << e.what() << std::endl;
    closeConnection(client_connection);
    // TODO: close origin conn if opened
  }
}

void Server::refreshPollfds() {
  getPollfdsServerIter()->revents = 0;
  auto pollfds_it = getPollfdsClientsBeginIter();
  auto clients_it = clients_.begin();
  for (; pollfds_it != pollfds_.end() && clients_it != clients_.end();
       ++pollfds_it, ++clients_it) {
    if (clients_it->client_connection != nullptr) {
      *pollfds_it = {clients_it->client_connection->getFileDescriptor(), POLLIN,
                     0};
    }
  }
}

}  // namespace spx
