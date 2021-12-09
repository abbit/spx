#include <ulimit.h>

#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "common/exception.hpp"
#include "server.hpp"

namespace {
static const int PORT_MIN = 1;
static const int PORT_MAX = 65535;

class Arguments {
 public:
  uint16_t port;
  rlim_t max_fds{1024};

  Arguments() = default;

  void parse(int argc, char* argv[]) {
    if (argc != 2) {
      std::stringstream ss;
      ss << "Usage: " << argv[0] << " PORT";
      throw std::invalid_argument(ss.str());
    }

    int port = atoi(argv[1]);
    if (port < PORT_MIN || port > PORT_MAX) {
      std::stringstream ss;
      ss << "Port must be in range [" << PORT_MIN << ", " << PORT_MAX << "]";
      throw std::invalid_argument(ss.str());
    }
    this->port = static_cast<uint16_t>(port);

    struct rlimit fd_limits;
    if (getrlimit(RLIMIT_NOFILE, &fd_limits) == 0) {
      max_fds = fd_limits.rlim_cur;
    } else {
      std::cerr << "can't get file descriptors limit" << std::endl;
    }
    // TODO: this is for debug, delete after
    max_fds = 5;
  }
};
}  // namespace

int main(int argc, char* argv[]) {
  Arguments args;
  try {
    args.parse(argc, argv);
  } catch (const std::invalid_argument& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Max file descriptors: " << args.max_fds << std::endl;

  try {
    spx::Server Server(args.port, args.max_fds);
    Server.start();
  } catch (const spx::Exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
