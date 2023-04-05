#pragma once

#include <map>
#include <sstream>
#include <string>

#include "../common/exception.h"
#include "httpparser/httprequestparser.hpp"
#include "httpparser/request.hpp"
#include "httpparser/urlparser.hpp"

namespace spx {
class HttpRequest {
 public:
  int versionMajor{1};
  int versionMinor{0};
  std::string method;
  httpparser::UrlParser url;
  std::map<std::string, std::string> headers;

  HttpRequest();

  explicit HttpRequest(const std::string &request_str);

  void parse(const std::string &request_str);

  std::string toString() const;

 private:
};
}  // namespace spx
