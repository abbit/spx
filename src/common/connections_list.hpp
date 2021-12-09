#pragma once

/*
#include <poll.h>

#include <vector>

#include "tcp_socket.hpp"

namespace spx {
class ConnnectionsList {
 public:
  ConnnectionsList(rlim_t max_fds) : max_fds_(max_fds) {}

  void refresh();
  void add();
  void remove(const int& fd);

 private:
  rlim_t max_fds_;
  std::vector<pollfd> pollfds_;
  std::vector<TcpSocket> connections_;
};
}  // namespace spx

*/