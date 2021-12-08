#include <ulimit.h>

#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "common/exception.hpp"
#include "server.hpp"

struct arguments {
  int port;
};

arguments get_arguments(int argc, char* argv[]) {
  arguments args;

  if (argc != 2) {
    std::stringstream ss;
    ss << "Usage: " << argv[0] << " PORT";
    throw std::invalid_argument(ss.str());
  }

  args.port = atoi(argv[1]);

  return args;
}

int main(int argc, char* argv[]) {
  arguments args;
  try {
    args = get_arguments(argc, argv);
  } catch (const std::invalid_argument& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  rlim_t max_fds = 1024;

  struct rlimit fd_limits;
  if (getrlimit(RLIMIT_NOFILE, &fd_limits) == 0) {
    max_fds = fd_limits.rlim_cur;
  } else {
    std::cerr << "can't get file descriptors limit" << std::endl;
  }

  std::cout << "Max file descriptors: " << max_fds << std::endl;

  try {
    spx::server server(args.port, max_fds);
    server.start();
  } catch (const spx::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}