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
#include <httpparser/httprequestparser.hpp>
#include <httpparser/request.hpp>
#include <httpparser/urlparser.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "common/exception.hpp"

static const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
static const int TIMEOUT_TIME = 600000;  // in ms

namespace spx {
server::server(int port, rlim_t max_fds) : max_fds(max_fds), pollfds(max_fds) {
  if (port < 1 || port > 65355) {
    throw exception("Port must be in range [1, 65355]");
  }
  this->port = port;

  /*
   * setup socket
   */

  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(nullptr, std::to_string(port).c_str(), &hints, &res) != 0) {
    throw exception("Failed to get addrinfo");
  }

  int server_sockfd =
      socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (server_sockfd < 0) {
    throw exception("Failed to create socket");
  }

  if (bind(server_sockfd, res->ai_addr, res->ai_addrlen) != 0) {
    throw exception("Failed to bind socket");
  }

  freeaddrinfo(res);

  /*
   * setup pollfd structs
   */

  for (auto &pfd : pollfds) {
    pfd.fd = -1;
    pfd.events = POLLIN;
  }
  get_server_pollfd().fd = server_sockfd;
  refresh_revents();
}

server::~server() {
  std::cout << "server destructor called" << std::endl;
  for (auto &pfd : pollfds) {
    close_connection(pfd);
  }
}

void server::refresh_revents() {
  for (auto &pfd : pollfds) {
    pfd.revents = 0;
  }
}

void server::accept_connection() {
  int conn_fd = accept(get_server_pollfd().fd, NULL, NULL);
  if (conn_fd < 0) {
    throw exception("error on accepting connection");
  }

  auto pfd = find_if(pollfds.begin(), pollfds.end(),
                     [](const pollfd &p) { return p.fd == -1; });
  if (pfd == pollfds.end()) {
    throw exception("Connections limit exceeded!");
    // TODO: close conn?
    close(conn_fd);
  }

  pfd->fd = conn_fd;

  std::cout << "Connection accepted\n" << std::endl;
}

void server::close_connection(pollfd &pfd) {
  if (pfd.fd > 0) {
    close(pfd.fd);
  }
  pfd.fd = -1;

  std::cout << "Connection closed" << std::endl;
}

void server::process_connection(const int &connection) {
  std::cout << "Started processinng connection #" << connection << std::endl;
  /*
   * read request from socket
   */
  char request_raw[1000000];
  char buffer[BUFSIZ];
  int message_size;
  int written = 0;
  while ((message_size = read(connection, buffer, sizeof(buffer))) > 0) {
    strncpy(request_raw + written, buffer, message_size);
    written += message_size;
    if (strstr(request_raw, "\r\n\r\n") != nullptr) break;
    // write(STDOUT_FILENO, buffer, message_size);
  }

  if (message_size < 0) {
    throw exception("error on reading from socket");
    // TODO: drop conn
  }

  /*
   * parse request
   */

  using namespace httpparser;

  Request request;
  HttpRequestParser::ParseResult res =
      HttpRequestParser().parse(request, request_raw, request_raw + written);
  if (res != HttpRequestParser::ParsingCompleted) {
    std::stringstream ss;
    ss << "Parsing failed (code " << res << ")\n"
       << "Request:\n"
       << request_raw;
    throw exception(ss.str());
  }

  /*
   * get ip by hostname
   */

  auto hostname = find_if(
      request.headers.begin(), request.headers.end(),
      [](const Request::HeaderItem &item) { return item.name == "Host"; });
  if (hostname == request.headers.end()) {
    throw exception("Host header was not provided");
    // TODO: close connection
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *servinfo;
  if (getaddrinfo(hostname->value.c_str(), "http", &hints, &servinfo) != 0) {
    throw exception("error on getaddrinfo");
    // TODO: close conn
  }

  /*
   * connect to host
   */

  int sock_fd = -1;
  for (struct addrinfo *p = servinfo; p != NULL; p = p->ai_next) {
    sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock_fd == -1) continue;

    if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == 0) {
      struct sockaddr_in *in = (struct sockaddr_in *)p->ai_addr;
      printf("Connected to %s (%s), port %d\n", hostname->value.c_str(),
             inet_ntoa(in->sin_addr), ntohs(in->sin_port));
      break;
    }

    close(sock_fd);
  }

  std::cout << "Got Request:\n" << request.inspect() << std::endl;

  /*
   * prepare request
   */

  static const std::vector<std::string> headers_to_remove{
      "Proxy-Connection", "Upgrade-Insecure-Requests", "Accept-Encoding"};

  request.headers.erase(
      remove_if(request.headers.begin(), request.headers.end(),
                [](const Request::HeaderItem &item) {
                  return find(headers_to_remove.begin(),
                              headers_to_remove.end(),
                              item.name) != headers_to_remove.end();
                }),
      request.headers.end());

  auto connection_header =
      find_if(request.headers.begin(), request.headers.end(),
              [](const Request::HeaderItem &item) {
                return item.name == "Connection";
              });
  if (connection_header != request.headers.end()) {
    connection_header->value = "close";
  }
  request.uri = UrlParser(request.uri).path();

  /*
   * send request
   */

  std::string request_str = request.inspect();
  size_t request_str_len = std::char_traits<char>::length(request_str.c_str());

  std::cout << "Send Request:\n" << request_str.c_str() << std::endl;

  long long written_size = write(sock_fd, request_str.c_str(), request_str_len);
  if (written_size < 0) {
    throw exception("error on writing to conn socket");
    // TODO: close conn
  }

  if (static_cast<size_t>(written_size) != request_str_len) {
    throw exception("partial write");
    // TODO: close conn
  }

  shutdown(sock_fd, SHUT_WR);

  /*
   * get response send response back to requester
   */

  std::cout << "Response:" << std::endl;
  size_t buffer_size = sizeof(buffer);
  // TODO: blocking, fix
  while ((message_size = read(sock_fd, buffer, buffer_size)) > 0) {
    written_size = write(connection, buffer, buffer_size);
    if (written_size < 0) {
      throw exception("error on writing to socket");
      // TODO: close conn
    }
    // std::cout << buffer;
  }
  std::cout << std::endl;
}

void server::start() {
  if (listen(get_server_pollfd().fd, PENDING_CONNECTIONS_QUEUE_LEN) != 0) {
    throw exception("error on listen");
  }

  std::cout << "Listening on port " << port << std::endl;

  for (;;) {
    int poll_result = poll(pollfds.data(), pollfds.size(), TIMEOUT_TIME);

    if (poll_result < 0) {
      throw exception("Error on poll");
    }

    if (poll_result == 0) {
      break;
    }

    for (auto &pfd : pollfds) {
      pollfd &server_pfd = get_server_pollfd();
      if (pfd.fd == server_pfd.fd && pfd.revents == POLLIN) {
        try {
          accept_connection();
        } catch (const exception &e) {
          std::cerr << e.what() << std::endl;
        }
      } else if ((pfd.revents & POLLHUP) > 0) {
        close_connection(pfd);
      } else if ((pfd.revents & POLLIN) > 0) {
        std::cout << "Revents: " << pfd.revents << std::endl;
        try {
          process_connection(pfd.fd);
        } catch (const exception &e) {
          std::cerr << e.what() << std::endl;
        }
      }
    }

    refresh_revents();
  }
}
}  // namespace spx