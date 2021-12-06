#include <ulimit.h>

#include <iostream>

#include "server.hpp"

int main(int argc, char* argv[]) {
  using namespace std;

  unsigned int port = 7331;
  rlim_t max_fds = 1024;

  struct rlimit fd_limits;
  if (getrlimit(RLIMIT_NOFILE, &fd_limits) == 0) {
    max_fds = fd_limits.rlim_cur;
  } else {
    cerr << "can't get file descriptors limit" << endl;
  }

  cout << "Max file descriptors: " << max_fds << endl;

  spx::server server(port, max_fds);
  server.start();

  return EXIT_SUCCESS;
}