#include "http_request.h"

namespace spx {
namespace {
const char *HEADERS_TO_REMOVE[] = {
    "Proxy-Connection",
    "Upgrade-Insecure-Requests",
};

httpparser::Request parseRequestStr(const std::string &request_str) {
  httpparser::Request request;
  httpparser::HttpRequestParser::ParseResult res =
      httpparser::HttpRequestParser().parse(request, request_str);
  if (res != httpparser::HttpRequestParser::ParsingCompleted) {
    std::stringstream ss;
    ss << "Parsing failed (code " << res << ")\n"
       << "Request:\n"
       << request_str;
    throw Exception(ss.str());
  }

  return request;
}
}  // namespace

HttpRequest::HttpRequest() = default;

HttpRequest::HttpRequest(const std::string &request_str) { parse(request_str); }

void HttpRequest::parse(const std::string &request_str) {
  httpparser::Request r = parseRequestStr(request_str);
  versionMajor = r.versionMajor;
  versionMinor = r.versionMinor;
  method = r.method;
  url.parse(r.uri);
  for (const auto &h : r.headers) {
    // check that header no in to-remove list
    if (find(std::begin(HEADERS_TO_REMOVE), std::end(HEADERS_TO_REMOVE),
             h.name) == std::end(HEADERS_TO_REMOVE)) {
      if (h.name == "Connection") {
        headers.insert_or_assign(h.name, "close");
      } else {
        headers.insert_or_assign(h.name, h.value);
      }
    }
  }
}

std::string HttpRequest::toString() const {
  std::stringstream stream;
  stream << method << " " << url.path() << " HTTP/" << versionMajor << "."
         << versionMinor << httpparser::CRLF;
  for (const auto &header : headers) {
    stream << header.first << ": " << header.second << httpparser::CRLF;
  }
  stream << httpparser::CRLF << httpparser::CRLF;

  return stream.str();
}
}  // namespace spx
