#include <poll.h>

#include <string>
#include <vector>

namespace spx {

class server {
 public:
  server(unsigned int port, rlim_t max_fds);
  ~server();
  server(const server&) = delete;
  server& operator=(const server&) = delete;

  void start();

 private:
  rlim_t max_fds;
  unsigned int port;
  std::vector<pollfd> pollfds;

  constexpr pollfd& get_server_pollfd() { return pollfds.at(max_fds - 1); };

  void refresh_revents();
  int accept_connection();
  int process_connection(const int& connection);
  int close_connection(pollfd& pollfd);
};
}  // namespace spx