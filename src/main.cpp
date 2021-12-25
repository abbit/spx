#include <iostream>
#include <sstream>
#include <stdexcept>

#include "common/exception.h"
#include "server.h"

namespace {
const int PORT_MIN = 1;
const int PORT_MAX = 65535;

class Arguments {
 public:
  uint16_t port{};

  Arguments() = default;

  void parse(int argc, char* argv[]) {
    if (argc != 2) {
      std::stringstream ss;
      ss << "Usage: " << argv[0] << " PORT";
      throw std::invalid_argument(ss.str());
    }

    int port_unchecked = std::stoi(std::string(argv[1]));
    if (port_unchecked < PORT_MIN || port_unchecked > PORT_MAX) {
      std::stringstream ss;
      ss << "Port must be in range [" << PORT_MIN << ", " << PORT_MAX << "]";
      throw std::invalid_argument(ss.str());
    }
    this->port = static_cast<uint16_t>(port_unchecked);
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

  try {
    spx::Server(args.port).start();
  } catch (const spx::Exception& e) {
    std::cerr << "My ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    std::cerr << "std::ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
