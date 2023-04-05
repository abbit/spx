#include "server.h"

#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "common/exception.h"
#include "http/http_request.h"
#include "net/passive_socket.h"

namespace spx {

namespace {
const int TIMEOUT = 600000;  // in ms
const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
const int MAX_CACHE_SIZE = 16 * 1024 * 1024;  // 16 mb
const size_t MAX_CHUNK_SIZE = 1024;           // 1 kb

// Utility functions

std::unique_ptr<ActiveSocket> connectToHost(const std::string &hostname,
                                            const std::string &port) {
  // get addrinfo by hostname
  addrinfo hints{};
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *servinfo;
  if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &servinfo) != 0) {
    throw Exception("error on getaddrinfo()");
  }

  // connect to host
  std::unique_ptr<ActiveSocket> res = nullptr;
  for (addrinfo *p = servinfo; p != nullptr; p = p->ai_next) {
    try {
      std::unique_ptr<ActiveSocket> socket = ActiveSocket::create(p);
      socket->connect(p->ai_addr, p->ai_addrlen);
      res = std::move(socket);
      break;
    } catch (const Exception &e) {
    }
  }
  freeaddrinfo(servinfo);

  if (!res || !res->isValid()) {
    std::stringstream ss;
    ss << "Can't connect to host " << hostname << ":" << port;
    throw Exception(ss.str());
  }

  return res;
}

void prepareForSendingRequest(Client &client) {
  client.server_connection =
      connectToHost(client.request.url.hostname(),
                    std::to_string(client.request.url.httpPort()));
  client.state = Client::State::sending_request;
}

std::vector<char> readResponseChunk(Client &client) {
  char chunk[MAX_CHUNK_SIZE];
  size_t message_size = client.server_connection->receive(chunk, sizeof(chunk));
  return {chunk, chunk + message_size};
}

}  // namespace

bool Server::is_running_ = false;

void Server::stop(int) { Server::is_running_ = false; }

Server::Server(uint16_t port, rlim_t max_fds) : clients_(max_fds) {
  server_socket_ = PassiveSocket::create(port);
  cache_ = Cache::create(MAX_CACHE_SIZE);

  signal(SIGTERM, Server::stop);
  signal(SIGINT, Server::stop);
  signal(SIGPIPE, SIG_IGN);
}

Server::~Server() { std::cout << "Server destructor called" << std::endl; }

void Server::addServerPollfd(std::vector<pollfd> &pfds) {
  pfds.push_back({server_socket_->getFileDescriptor(), POLLIN, 0});
}

std::vector<pollfd> Server::getPollfds() {
  std::vector<pollfd> pfds = clients_.getPollfds();
  addServerPollfd(pfds);
  return pfds;
}

std::vector<pollfd> Server::poll() {
  std::vector<pollfd> pfds = getPollfds();

  int poll_result = ::poll(pfds.data(), pfds.size(), TIMEOUT);

  if (poll_result < 0) {
    throw Exception("error on poll()");
  }

  if (poll_result == 0) {
    std::cout << "poll() timed out" << std::endl;
    return {};
  }

  pfds.erase(std::remove_if(pfds.begin(), pfds.end(),
                            [](const pollfd &pfd) { return pfd.revents == 0; }),
             pfds.end());

  return pfds;
}

void Server::start() {
  server_socket_->listen(PENDING_CONNECTIONS_QUEUE_LEN);
  std::cout << "Listening on port " << server_socket_->getPort() << std::endl;

  is_running_ = true;
  while (is_running_) {
    std::vector<pollfd> pfds = poll();

    for (const auto &pfd : pfds) {
      if (*server_socket_ == pfd.fd) {
        handleServerSocketEvents(pfd.revents);
      } else {
        handleClientSocketEvents(clients_.getClient(pfd.fd), pfd.revents);
      }
    }
  }
}

void Server::handleServerSocketEvents(const int &events_bitmask) {
  if (events_bitmask & (POLLERR | POLLNVAL)) {
    throw Exception("Error on server socket");
  }

  if (events_bitmask & POLLIN) {
    try {
      acceptClient();
    } catch (const Exception &e) {
      std::cerr << "Error on accepting connection to server: " << e.what()
                << std::endl;
    }
  }
}

void Server::handleClientSocketEvents(Client &client, const int &revents) {
  try {
    if (revents & (POLLERR | POLLNVAL)) {
      discardClient(client);
      return;
    }

    switch (client.state) {
      case Client::State::waiting_for_request: {
        readRequestFromClient(client);
        break;
      }
      case Client::State::waiting_for_response_for_another_client:
        // not reachable, because client in this state does not set events flags
        break;
      case Client::State::sending_request: {
        if (revents & POLLHUP) {
          // server hangup, cant send request
          discardClient(client);
          return;
        }
        sendRequestToServer(client);
        break;
      }
      case Client::State::waiting_response_status_code: {
        readResponseStatusCodeFromServer(client);
        break;
      }
      case Client::State::waiting_response:
        if (revents & POLLIN) {
          readResponseFromServer(client);
        }
      case Client::State::getting_response_from_another_client:
      case Client::State::got_response: {
        if (revents == POLLHUP) {
          // client hangup, cant send response
          discardClient(client);
          return;
        }

        if (revents & POLLOUT) {
          sendResponseToClient(client);
        }
        break;
      }
    }
  } catch (const Exception &e) {
    std::cerr << "Error while handling client socket events: " << e.what()
              << std::endl;
  }
}

void Server::acceptClient() { clients_.acceptClient(server_socket_->accept()); }

void Server::discardClient(Client &client) {
  std::cout << "Discarding " << client << std::endl;

  auto &request_cliest_list = request_clients_map[client.request_str];
  removeFromRequestClients(client);

  if (client.state == Client::State::waiting_response &&
      hasClientsWithRequest(client.request_str)) {
    std::cout << "has other client with this request" << std::endl;
    const auto &reading_from_cache_client =
        std::find_if(request_cliest_list.begin(), request_cliest_list.end(),
                     [](const std::reference_wrapper<Client> &client) {
                       return client.get().isReadingFromCache();
                     });

    if (reading_from_cache_client != request_cliest_list.end()) {
      std::cout << "has other client that reading from cache" << std::endl;
      reading_from_cache_client->get().obtainServerConnection(client);
    }
  }

  cache_->disuseEntry(client.request_str);
  clients_.discardClient(client);
}

void Server::writeResponseChunkToClientBuffer(Client &client,
                                              const std::vector<char> &chunk) {
  std::cout << client << "writing chunk to buffer" << std::endl;
  client.chunks.push_back(chunk);
}

void Server::writeResponseChunkToCache(Client &client,
                                       const std::vector<char> &chunk) {
  std::cout << client << "writing chunk to cache" << std::endl;
  try {
    cache_->write(client.request_str, chunk.data(), chunk.size());
  } catch (const AllInUseException &e) {
    std::cout << client << e.what() << ", fallback to buffer" << std::endl;
    fallbackToClientBuffer(client, chunk);
  }
}

void Server::fallbackToClientBuffer(Client &original_client,
                                    const std::vector<char> &chunk) {
  for (auto &c : request_clients_map[original_client.request_str]) {
    auto &client = c.get();
    client.should_read_from_cache = false;
    client.state = Client::State::getting_response_from_another_client;
    auto &entry = cache_->getEntry(client.request_str);
    if (client.sent_bytes < entry.size()) {
      client.chunks.emplace_back(entry.data() + client.sent_bytes,
                                 entry.data() + entry.size());
    }
    writeResponseChunkToClientBuffer(client, chunk);
    cache_->disuseEntry(client.request_str);
  }

  original_client.should_write_to_cache = false;
  original_client.should_write_to_all = true;
  original_client.state = Client::State::waiting_response;
}

void Server::setClientToGetResponseFromCache(Client &client) {
  client.state = Client::State::got_response;
  cache_->useEntry(client.request_str);
  client.should_read_from_cache = true;
}

void Server::readRequestFromClient(Client &client) {
  std::cout << client << "Read request from client" << std::endl;

  try {
    client.getHttpRequest();

    // if response to request already in cache, then just use it
    // otherwise send request to server
    if (cache_->contains(client.request_str)) {
      std::cout << "Have response in cache!" << std::endl;
      setClientToGetResponseFromCache(client);
    } else if (hasClientsWithRequest(client.request_str)) {
      std::cout << "Has another client with this request, wait for his response"
                << std::endl;
      client.state = Client::State::waiting_for_response_for_another_client;
    } else {
      std::cout << "Response is not in cache" << std::endl;
      prepareForSendingRequest(client);
    }

    addToRequestClients(client);
  } catch (const Exception &e) {
    std::cerr << "Error while reading request from client: " << e.what()
              << std::endl;
    discardClient(client);
  }
}

void Server::addToRequestClients(Client &client) {
  if (request_clients_map.find(client.request_str) !=
      request_clients_map.end()) {
    request_clients_map[client.request_str].push_back(client);
  } else {
    request_clients_map[client.request_str] = {client};
  }
}

void Server::removeFromRequestClients(Client &client) {
  auto &request_cliest_list = request_clients_map[client.request_str];

  request_cliest_list.erase(
      std::remove_if(
          request_cliest_list.begin(), request_cliest_list.end(),
          [&client](std::reference_wrapper<Client> &c) { return client == c; }),
      request_cliest_list.end());
}

bool Server::hasClientsWithRequest(const std::string &request) {
  auto clients_list = request_clients_map.find(request);
  return clients_list != request_clients_map.end() &&
         !clients_list->second.empty();
}

void Server::sendRequestToServer(Client &client) {
  std::cout << client << "Send request to server" << std::endl;

  try {
    ActiveSocket &server_conn = *client.server_connection;

    server_conn.send(client.request_str);
    server_conn.shutdownWrite();

    client.state = Client::State::waiting_response_status_code;
  } catch (const Exception &e) {
    std::cerr << "Error while sending request to server: " << e.what()
              << std::endl;
    discardClient(client);
  }
}

void Server::prepareAllWaitingClientsForSending(const std::string &request) {
  for (auto &c : request_clients_map[request]) {
    if (c.get().state ==
        Client::State::waiting_for_response_for_another_client) {
      prepareForSendingRequest(c.get());
    }
  }
}

void Server::readResponseStatusCodeFromServer(Client &client) {
  std::cout << client << "Read response status code from server" << std::endl;

  try {
    std::vector<char> chunk = readResponseChunk(client);

    const size_t STATUS_CODE_OFFSET = 9;
    const size_t STATUS_CODE_LEN = 3;
    const size_t OK_STATUS_CODE = 200;

    std::string status_code{chunk.data() + STATUS_CODE_OFFSET, STATUS_CODE_LEN};
    std::cout << "Status code=" << status_code << std::endl;
    bool is_ok_status_code = std::stoi(status_code) == OK_STATUS_CODE;

    if (is_ok_status_code) {
      client.should_write_to_cache = true;
      client.should_read_from_cache = true;
      cache_->useEntry(client.request_str);
      writeResponseChunkToCache(client, chunk);
      for (auto &c : request_clients_map[client.request_str]) {
        if (client != c) {
          setClientToGetResponseFromCache(c.get());
        }
      }
    } else {
      writeResponseChunkToClientBuffer(client, chunk);
      prepareAllWaitingClientsForSending(client.request_str);
    }
    client.state = Client::State::waiting_response;
  } catch (const Exception &e) {
    std::cerr << "Error while reading response status code from server: "
              << e.what() << std::endl;
    prepareAllWaitingClientsForSending(client.request_str);
    discardClient(client);
  }
}

void Server::readResponseFromServer(Client &client) {
  std::cout << client << "Read response from server" << std::endl;

  try {
    std::vector<char> chunk = readResponseChunk(client);

    if (client.should_write_to_cache) {
      writeResponseChunkToCache(client, chunk);
    } else if (client.should_write_to_all) {
      for (auto &c : request_clients_map[client.request_str]) {
        writeResponseChunkToClientBuffer(c.get(), chunk);
      }
    } else {
      writeResponseChunkToClientBuffer(client, chunk);
    }
  } catch (const ZeroLengthMessageException &e) {
    std::cout << "Done reading response" << std::endl;
    client.state = Client::State::got_response;
    if (client.should_write_to_cache) {
      cache_->getEntry(client.request_str).complete();
    } else if (client.should_write_to_all) {
      for (auto &c : request_clients_map[client.request_str]) {
        c.get().state = Client::State::got_response;
      }
    }
  } catch (const Exception &e) {
    std::cerr << "Error while reading response from server: " << e.what()
              << std::endl;
    discardClient(client);
  }
}

void Server::sendResponseToClient(Client &client) {
  if (client.should_read_from_cache) {
    sendCachedResponseToClient(client);
  } else {
    sendUncachedResponseToClient(client);
  }
}

void Server::sendUncachedResponseToClient(Client &client) {
  try {
    if (client.chunks.empty()) {
      if (client.state == Client::State::got_response) {
        std::cout << "DONE!!!" << std::endl;
        discardClient(client);
      } else {
        //        std::cout << "No new data" << std::endl;
      }
      return;
    }

    std::cout << client << "Send response to client" << std::endl;

    client.sendToClientFromChunks();
  } catch (const Exception &e) {
    std::cerr << "Error while sending response to client: " << e.what()
              << std::endl;
    discardClient(client);
  }
}

void Server::sendCachedResponseToClient(Client &client) {
  try {
    const auto &cache_entry = cache_->getEntry(client.request_str);
    const size_t received_bytes = cache_entry.size();
    const bool is_new_data_available = client.sent_bytes < received_bytes;
    if (!is_new_data_available) {
      if (cache_entry.isCompleted()) {
        std::cout << "DONE!!!" << std::endl;
        discardClient(client);
      } else {
        //        std::cout << "No new data" << std::endl;
      }
      return;
    }

    std::cout << client << "Send cached response to client" << std::endl;

    size_t chunk_size =
        std::min(received_bytes - client.sent_bytes, MAX_CHUNK_SIZE);
    client.sendToClient(
        cache_->read(client.request_str, client.sent_bytes, chunk_size));
  } catch (const KeyNotFoundException &e) {
    std::cout << "no cache yet" << std::endl;
  } catch (const Exception &e) {
    std::cerr << "Error while sending response to client: " << e.what()
              << std::endl;
    discardClient(client);
  }
}

}  // namespace spx
