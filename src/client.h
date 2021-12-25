#pragma once

#include <sys/poll.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "common/active_socket.h"
#include "common/cache.h"
#include "common/http_request.h"

namespace spx {

class Client {
 public:
  enum class State : char {
    initial,
    //    get_from_cache,
    //    waiting_for_response_for_another_client,
    sending_request,
    waiting_response,
    got_response,
  };

  std::string request_str;
  HttpRequest request;
  std::unique_ptr<ActiveSocket> client_connection{nullptr};
  std::unique_ptr<ActiveSocket> server_connection{nullptr};
  State state{State::initial};
  size_t sent_bytes{0};
//  bool should_use_cache{false};
  bool done{false};

  static Client* create(std::unique_ptr<ActiveSocket> client_conn);

  bool operator==(const Client& other) const;
  bool operator!=(const Client& other) const;

  friend std::ostream& operator<<(std::ostream& os, const Client& that);

  void getHttpRequest();
  void sendToClient(const std::vector<char>& chunk);

  //  bool isReadingFromCache() const;
  //  void obtainServerConnection(Client& other);

 private:
  Client();
};

}  // namespace spx
