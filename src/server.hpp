#pragma once

#include <poll.h>

#include <string>
#include <vector>

namespace spx {

class Server {
 public:
  Server(int port, rlim_t max_fds);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  void start();

 private:
  rlim_t max_fds;
  unsigned int port;
  std::vector<pollfd> pollfds;

  constexpr pollfd& get_server_pollfd() { return pollfds.at(max_fds - 1); }

  void refresh_revents();
  void accept_connection();
  void process_connection(const int& connection);
  void close_connection(pollfd& pollfd);
};
}  // namespace spx
