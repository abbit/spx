#pragma once

#include <stdexcept>
#include <string>

namespace spx {
class exception : public std::runtime_error {
 public:
  exception(const std::string& message) : std::runtime_error(message) {}
};
}  // namespace spx