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
#include <string>
#include <vector>

static const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
static const int TIMEOUT_TIME = 600000;  // in ms

spx::server::server(unsigned int port, rlim_t max_fds)
    : port(port), max_fds(max_fds), pollfds(max_fds) {
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(nullptr, std::to_string(port).c_str(), &hints, &res) != 0) {
    std::cerr << "Error while trying to setup socket" << std::endl;
    exit(EXIT_FAILURE);
  }

  int server_sockfd =
      socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (server_sockfd < 0) {
    std::cerr << "Error while creating socket" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (bind(server_sockfd, res->ai_addr, res->ai_addrlen) != 0) {
    std::cerr << "Error while binding socket" << std::endl;
    perror("");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(res);

  for (auto &pollfd : pollfds) {
    pollfd.fd = -1;
    pollfd.events = POLLIN;
  }

  get_server_pollfd().fd = server_sockfd;
  refresh_revents();
}

spx::server::~server() {
  for (auto &pollfd : pollfds) {
    close_connection(pollfd);
  }
}

void spx::server::refresh_revents() {
  for (auto &pollfd : pollfds) {
    pollfd.revents = 0;
  }
}

int spx::server::accept_connection() {
  int conn_fd = accept(get_server_pollfd().fd, NULL, NULL);
  if (conn_fd < 0) {
    std::cerr << "error on accepting connection" << std::endl;
    return -1;
  }

  int accepted = 0;
  auto pfd = find_if(pollfds.begin(), pollfds.end(),
                     [](const pollfd &p) { return p.fd == -1; });
  if (pfd == pollfds.end()) {
    std::cerr << "Connections limit exceeded!" << std::endl;
    close(conn_fd);
    return -1;
  }
  pfd->fd = conn_fd;

  std::cout << "Connection accepted\n" << std::endl;

  return 0;
}

int spx::server::close_connection(pollfd &pollfd) {
  if (pollfd.fd > 0) {
    close(pollfd.fd);
  }
  pollfd.fd = -1;

  std::cout << "Connection closed" << std::endl;

  return 0;
}

int spx::server::process_connection(const int &connection) {
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
    std::cerr << "error on reading from socket" << std::endl;
    return -1;
    // TODO: drop conn
  }

  /*
   * parse request
   */

  using namespace httpparser;

  Request request;
  HttpRequestParser parser;

  HttpRequestParser::ParseResult res =
      parser.parse(request, request_raw, request_raw + written);

  if (res != HttpRequestParser::ParsingCompleted) {
    std::cerr << "Parsing failed - " << res << std::endl;
    std::cerr << "Request:\n" << request_raw << std::endl;
    return -1;
  }

  /*
   * get ip by hostname
   */

  auto hostname = find_if(
      request.headers.begin(), request.headers.end(),
      [](const Request::HeaderItem &item) { return item.name == "Host"; });
  if (hostname == request.headers.end()) {
    std::cerr << "Host header was not provided" << std::endl;
    // TODO: close connection
    return -1;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *servinfo;
  if (getaddrinfo(hostname->value.c_str(), "http", &hints, &servinfo) != 0) {
    std::cerr << "error on getaddrinfo" << std::endl;
    // TODO: close conn
    return -1;
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

  int written_size = write(sock_fd, request_str.c_str(), request_str_len);
  if (written_size < 0) {
    std::cerr << "error on writing to conn socket" << std::endl;
    // TODO: close conn
    return -1;
  }

  if (written_size != request_str_len) {
    std::cerr << "partial write" << std::endl;
    // TODO: close conn
    return -1;
  }

  shutdown(sock_fd, SHUT_WR);

  /*
   * get response
   */

  char resp[1000000];
  written = 0;
  while ((message_size = read(sock_fd, buffer, sizeof(buffer))) > 0) {
    strncpy(resp + written, buffer, message_size);
    written += message_size;
  }

  if (message_size < 0) {
    std::cerr << "error on reading from conn socket" << std::endl;
    // TODO: close conn
    return -1;
  }

  std::cout << "Response:\n" << resp << std::endl;

  /*
   * send response back to requester
   */

  written_size = write(connection, resp, written);
  if (written_size < 0) {
    std::cerr << "error on writing to socket" << std::endl;
    // TODO: close conn
    return -1;
  }

  return 0;
}

void spx::server::start() {
  if (listen(get_server_pollfd().fd, PENDING_CONNECTIONS_QUEUE_LEN) != 0) {
    std::cerr << "error on listen" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Listening on port " << port << std::endl;

  for (;;) {
    int poll_result = poll(pollfds.data(), pollfds.size(), TIMEOUT_TIME);

    if (poll_result < 0) {
      std::cerr << "Error on poll" << std::endl;
      exit(EXIT_FAILURE);
    }

    if (poll_result == 0) {
      break;
    }

    if (get_server_pollfd().revents == POLLIN) {
      accept_connection();
    }

    for (auto &pollfd : pollfds) {
      if ((pollfd.revents & POLLHUP) > 0) {
        close_connection(pollfd);
      } else if ((pollfd.revents & POLLIN) > 0) {
        std::cout << "Revents:\n" << pollfd.revents << std::endl;
        process_connection(pollfd.fd);
      }
    }

    refresh_revents();
  }
}