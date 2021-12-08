#pragma once

#include <stdexcept>
#include <string>

namespace spx {
class Exception : public std::runtime_error {
 public:
  explicit Exception(const std::string& message)
      : std::runtime_error(message) {}
};
}  // namespace spx
