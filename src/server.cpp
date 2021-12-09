#include "server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "common/exception.hpp"
#include "common/http_request.hpp"
#include "common/server_socket.hpp"
#include "httpparser/httprequestparser.hpp"
#include "httpparser/request.hpp"
#include "httpparser/urlparser.hpp"

namespace spx {
namespace http = httpparser;

static const int TIMEOUT = 600000;  // in ms
static const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
static const int MAX_REQUEST_SIZE = 16 * 1024;  // 16 kb

Server::Server(uint16_t port, rlim_t max_fds)
    : server_socket_(port), pollfds_(max_fds) {
  for (auto &pfd : pollfds_) {
    pfd = {INVALID_SOCKET_FD, POLL_IN, 0};
  }
  pollfds_.begin()->fd = server_socket_.getFileDescriptor();
}

Server::~Server() { std::cout << "Server destructor called" << std::endl; }

void Server::start() {
  server_socket_.listen(PENDING_CONNECTIONS_QUEUE_LEN);
  std::cout << "Listening on port " << server_socket_.getPort() << std::endl;

  for (;;) {
    /*
     * setup pollfd struct
     */
    auto pollfds_it = ++pollfds_.begin();
    for (const auto &client : clients_) {
      assert(pollfds_it != pollfds_.end());
      pollfds_it->fd = client.first;
      ++pollfds_it;
    }
    refreshRevents();

    int poll_result = poll(pollfds_.data(), clients_.size() + 1, TIMEOUT);

    if (poll_result < 0) {
      std::cerr << "error on poll()" << std::endl;
      continue;
    }

    if (poll_result == 0) {
      std::cout << "poll() timed out" << std::endl;
      continue;
    }

    for (const auto &pfd : pollfds_) {
      // TODO: handle POLLERR
      if (server_socket_.getFileDescriptor() == pfd.fd &&
          pfd.revents == POLLIN) {
        try {
          acceptConnection();
        } catch (const Exception &e) {
          std::cerr << e.what() << std::endl;
        }
      } else if ((pfd.revents & POLLHUP) > 0) {
        closeConnection(clients_[pfd.fd]);
      } else if ((pfd.revents & POLLIN) > 0) {
        try {
          handleConnection(clients_[pfd.fd]);
        } catch (const Exception &e) {
          std::cerr << e.what() << std::endl;
        }
      }
    }
  }
}

void Server::acceptConnection() {
  TcpSocket conn = server_socket_.accept();
  int id = conn.getFileDescriptor();
  clients_[id] = std::move(conn);
  std::cout << "\nConnection accepted " << clients_[id] << std::endl
            << std::endl;
}

void Server::closeConnection(const TcpSocket &connection) {
  clients_.erase(connection.getFileDescriptor());
  std::cout << "Connection closed " << connection << std::endl;
}

std::string readRequest(const TcpSocket &connection) {
  char request_raw[MAX_REQUEST_SIZE];
  char buffer[BUFSIZ];
  int message_size;
  int written = 0;
  while ((message_size = connection.receive(buffer, sizeof(buffer))) > 0) {
    strncpy(request_raw + written, buffer, message_size);
    written += message_size;
    // TODO: check request_raw bounds
    if (strstr(request_raw, "\r\n\r\n") != nullptr) break;
  }

  if (message_size < 0) {
    throw Exception("error on reading from socket");
    // TODO: drop conn
  }

  return std::string(request_raw, written);
}

TcpSocket connectToHost(const std::string &hostname, const std::string &port) {
  // get addrinfo by hostname
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *servinfo;
  if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &servinfo) != 0) {
    throw Exception("error on getaddrinfo()");
    // TODO: close conn
  }

  // connect to host
  int sock_fd = INVALID_SOCKET_FD;
  for (struct addrinfo *p = servinfo; p != nullptr; p = p->ai_next) {
    sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock_fd == INVALID_SOCKET_FD) continue;

    if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == 0) {
      struct sockaddr_in *in = (struct sockaddr_in *)p->ai_addr;
      std::cout << "Connected to " << hostname << "(" << inet_ntoa(in->sin_addr)
                << ")"
                << ", port " << ntohs(in->sin_port) << std::endl;
      break;
    }

    close(sock_fd);
    sock_fd = INVALID_SOCKET_FD;
  }
  freeaddrinfo(servinfo);

  if (sock_fd == INVALID_SOCKET_FD) {
    std::stringstream ss;
    ss << "Can't connect to host " << hostname << ":" << port;
    throw Exception(ss.str());
  }

  return TcpSocket(sock_fd);
}

void translateData(const TcpSocket &from, const TcpSocket &to) {
  static char buffer[BUFSIZ];
  size_t buffer_size = sizeof(buffer);
  int message_size;
  int64_t written_size;
  // TODO: blocking, fix
  while ((message_size = from.receive(buffer, buffer_size)) > 0) {
    written_size = to.send(buffer, buffer_size);
    if (written_size < 0) {
      throw Exception("error on writing to socket");
      // TODO: close conn
    }

    if (static_cast<size_t>(written_size) != buffer_size) {
      throw Exception("partial write");
      // TODO: close conn or not???
    }
  }
}

void Server::handleConnection(const TcpSocket &connection) {
  std::cout << "\nHandling connection " << connection << std::endl << std::endl;

  std::string received_request_str = readRequest(connection);
  std::cout << "Received Request:\n" << received_request_str << std::endl;
  HttpRequest request(received_request_str);

  // TODO: save origin connection
  TcpSocket origin = connectToHost(request.url.hostname(),
                                   std::to_string(request.url.httpPort()));
  origin.send(request.toString());
  // TODO: remove!
  shutdown(origin.getFileDescriptor(), SHUT_WR);
  std::cout << "\nSend Request:\n" << request.toString() << std::endl;

  // get response send response back to requester
  translateData(origin, connection);
  std::cout << "\nSend response\n" << std::endl;
}

void Server::refreshRevents() {
  for (auto &pfd : pollfds_) {
    pfd.revents = 0;
  }
}
}  // namespace spx
