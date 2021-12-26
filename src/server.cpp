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
#include "common/http_request.h"
#include "common/passive_socket.h"

namespace spx {

namespace http = httpparser;

namespace {
const int PENDING_CONNECTIONS_QUEUE_LEN = 5;
const int MAX_CACHE_SIZE = 64 * 1024 * 1024;  // 16 mb
const size_t MAX_CHUNK_SIZE = 16 * 1024;      // 16 kb

// --- Utility functions ---

std::unique_ptr<ActiveSocket> connectToHost(const std::string &hostname,
                                            const std::string &port) {
  std::cout << "connecting to host " << hostname << ":" << port << std::endl;
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

  std::cout << "connected to host " << hostname << ":" << port << std::endl;

  return res;
}

void prepareForSendingRequest(Client &client) {
  client.server_connection =
      connectToHost(client.request.url.hostname(),
                    std::to_string(client.request.url.httpPort()));
  client.state = Client::State::sending_request;
}

void prepareAllWaitingClientsForSending(
    std::list<std::reference_wrapper<Client>> &list) {
  for (auto &c : list) {
    if (c.get().state ==
        Client::State::waiting_for_response_status_code_for_another_client) {
      prepareForSendingRequest(c.get());
    }
  }
}

bool hasClientWithSendingRequest(
    const std::list<std::reference_wrapper<Client>> &list) {
  auto is_sending_request = [](std::reference_wrapper<Client> c) {
    return c.get().state == Client::State::sending_request;
  };

  return std::find_if(list.begin(), list.end(), is_sending_request) !=
         list.end();
}

}  // namespace

bool Server::is_running_ = false;
auto Server::cache_ = Cache::create(MAX_CACHE_SIZE);
std::unordered_map<std::string, std::unique_ptr<RequestClientsMapEntry>>
    Server::request_clients_map;
Mutex Server::request_clients_map_mutex_;

void Server::stop(int) { Server::is_running_ = false; }

Server::Server(uint16_t port) {
  server_socket_ = PassiveSocket::create(port);

  signal(SIGTERM, Server::stop);
  signal(SIGINT, Server::stop);
  signal(SIGPIPE, SIG_IGN);
}

Server::~Server() { std::cout << "Server destructor called" << std::endl; }

void Server::start() {
  server_socket_->listen(PENDING_CONNECTIONS_QUEUE_LEN);
  std::cout << "Listening on port " << server_socket_->getPort() << std::endl;

  is_running_ = true;
  while (is_running_) {
    try {
      acceptClient();
    } catch (const Exception &e) {
      std::cerr << "Error on accepting connection to server: " << e.what()
                << std::endl;
    }
  }
}

void Server::acceptClient() {
  pthread_t id_;
  Client *client = Client::create(server_socket_->accept());
  if (pthread_create(&id_, nullptr, handleClient, client) != 0) {
    delete client;
    throw Exception("cant start client handler");
  }

  if (pthread_detach(id_) != 0) {
    delete client;
    throw Exception("cant detach client handler");
  }
}

void Server::createRequestClientsMapEntryIfDoesntExist(
    const std::string &request) {
  request_clients_map_mutex_.lock();
  if (request_clients_map.find(request) == request_clients_map.end()) {
    request_clients_map[request] = std::make_unique<RequestClientsMapEntry>();
  }
  request_clients_map_mutex_.unlock();
}

void Server::removeFromRequestClients(Client &client) {
  auto &entry = request_clients_map.at(client.request_str);
  entry->mutex.lock();
  entry->list.erase(
      std::remove_if(
          entry->list.begin(), entry->list.end(),
          [&client](std::reference_wrapper<Client> &c) { return client == c; }),
      entry->list.end());
  entry->mutex.unlock();
}

std::vector<char> Server::readResponseChunk(Client &client) {
  char chunk[MAX_CHUNK_SIZE];
  size_t message_size = client.server_connection->receive(chunk, sizeof(chunk));
  return {chunk, chunk + message_size};
}

void Server::discardClient(Client *client) {
  std::cout << "Discarding " << *client << std::endl;

  //  auto &entry = getRequestClientsMapEntry(client->request_str);
  //  entry.mutex->lock();
  //  if (client->state == Client::State::waiting_response &&
  //      hasClientsWithRequest(client->request_str)) {
  //    std::cout << "has other client with this request" << std::endl;
  //    const auto &reading_from_cache_client =
  //        std::find_if(entry.list.begin(), entry.list.end(),
  //                     [](const std::reference_wrapper<Client> &client) {
  //                       return client.get().isReadingFromCache();
  //                     });
  //
  //    if (reading_from_cache_client != entry.list.end()) {
  //      std::cout << "has other client that reading from cache" << std::endl;
  //      reading_from_cache_client->get().obtainServerConnection(*client);
  //    }
  //  }
  //  entry.mutex->unlock();
  //
  //  removeFromRequestClients(*client);

  if (client->should_use_cache) {
    cache_->disuseEntry(client->request_str);
  }

  delete client;
}

// void Server::fallbackToClientBuffer(Client &client) {
//   auto &entry = cache_->getEntry(client.request_str);
//   if (client.sent_bytes < entry.size()) {
//     client.sendToClient(
//         {entry.data() + client.sent_bytes, entry.data() + entry.size()});
//   }
//   client.should_use_cache = false;
//   cache_->disuseEntry(client.request_str);
// }

void Server::writeResponseChunkToCache(Client &client,
                                       const std::vector<char> &chunk) {
  std::cout << client << "writing chunk to cache" << std::endl;
  try {
    cache_->write(client.request_str, chunk.data(), chunk.size());
  } catch (const AllInUseException &e) {
    std::cout << client << e.what() << ", fallback to buffer" << std::endl;
    // TODO: переключить всех на буфер
    //    fallbackToClientBuffer(client);
  }
}

void Server::readRequestFromClient(Client &client) {
  std::cout << client << "Read request from client" << std::endl;

  client.getHttpRequest();
  std::cout << client.request_str << std::endl;

  createRequestClientsMapEntryIfDoesntExist(client.request_str);
  auto &entry = request_clients_map.at(client.request_str);
  entry->mutex.lock();

  entry->list.emplace_back(client);

  if (hasClientWithSendingRequest(entry->list)) {
    std::cout << client
              << "Has another client with this request, wait for his response"
              << std::endl;
    client.state =
        Client::State::waiting_for_response_status_code_for_another_client;
    return;
  } else {
    std::cout << client << "NONONO" << std::endl;
  }

  if (cache_->useEntryIfExists(client.request_str)) {
    std::cout << client << "Have response in cache!" << std::endl;
    client.state = Client::State::get_from_cache;
    client.should_use_cache = true;
  } else {
    std::cout << client << "Response is not in cache" << std::endl;
    prepareForSendingRequest(client);
  }

  entry->mutex.unlock();
}

void Server::sendRequestToServer(Client &client) {
  std::cout << client << "Send request to server" << std::endl;

  ActiveSocket &server_conn = *client.server_connection;
  server_conn.send(client.request_str);
  server_conn.shutdownWrite();
}

void Server::readResponseStatusCodeFromServer(Client &client) {
  std::cout << client << "Read response status code from server" << std::endl;

  auto &entry = request_clients_map.at(client.request_str);
  entry->mutex.lock();

  try {
    std::vector<char> chunk = readResponseChunk(client);

    const size_t STATUS_CODE_OFFSET = 9;
    const size_t STATUS_CODE_LEN = 3;
    const size_t OK_STATUS_CODE = 200;

    std::string status_code{chunk.data() + STATUS_CODE_OFFSET, STATUS_CODE_LEN};
    std::cout << "Status code=" << status_code << std::endl;
    bool is_ok_status_code = std::stoi(status_code) == OK_STATUS_CODE;

    if (is_ok_status_code) {
      client.should_use_cache = true;
      cache_->useEntry(client.request_str);
      writeResponseChunkToCache(client, chunk);
      for (auto &c : entry->list) {
        if (client != c) {
          client.state = Client::State::get_from_cache;
          client.should_use_cache = true;
        }
      }
    } else {
      prepareAllWaitingClientsForSending(entry->list);
    }

    client.state = Client::State::transferring_response;
    entry->cond_var.notifyAll();

    client.sendChunkToClient(chunk);
  } catch (const Exception &e) {
    prepareAllWaitingClientsForSending(entry->list);
  }

  entry->mutex.unlock();
}

void Server::transferResponseChunk(Client &client) {
  std::cout << client << "Transfer response chunk" << std::endl;

  try {
    std::vector<char> chunk = readResponseChunk(client);

    if (client.should_use_cache) {
      writeResponseChunkToCache(client, chunk);
    }

    client.sendChunkToClient(chunk);
  } catch (const ZeroLengthMessageException &e) {
    std::cout << "Done transferring response" << std::endl;
    if (client.should_use_cache) {
      cache_->completeEntry(client.request_str);
    }
    client.done = true;
  }
}

void Server::sendCachedResponseChunkToClient(Client &client) {
  std::cout << client << "Send cached response chunk" << std::endl;

  if (cache_->isEntryCompleted(client.request_str)) {
    client.done = true;
  }

  const size_t received_bytes = cache_->getEntrySize(client.request_str);
  size_t chunk_size = received_bytes - client.sent_bytes;
  std::cout << client << "BEFORE: sent_bytes=" << client.sent_bytes
            << std::endl;
  client.sendChunkToClient(
      cache_->read(client.request_str, client.sent_bytes, chunk_size));
  std::cout << client << "AFTER: sent_bytes=" << client.sent_bytes << std::endl;
}

void *Server::handleClient(void *arg) {
  auto *client = reinterpret_cast<Client *>(arg);

  try {
    std::cout << "Handling " << *client << std::endl;

    while (!client->done) {
      switch (client->state) {
        case Client::State::initial: {
          readRequestFromClient(*client);
          break;
        }
        case Client::State::
            waiting_for_response_status_code_for_another_client: {
          auto &entry = request_clients_map.at(client->request_str);
          // TODO: check for some predicate?
          entry->cond_var.wait(entry->mutex);
          entry->mutex.unlock();
          client->state = Client::State::get_from_cache;
          break;
        }
        case Client::State::sending_request: {
          sendRequestToServer(*client);
          readResponseStatusCodeFromServer(*client);
          break;
        }
        case Client::State::transferring_response: {
          transferResponseChunk(*client);
          break;
        }
        case Client::State::get_from_cache: {
          std::cout << *client << "wait for cache update" << std::endl;
          cache_->waitForEntryUpdate(client->request_str);
          sendCachedResponseChunkToClient(*client);
          break;
        }
        default:
          throw Exception("Unknown state");
      }
    }
  } catch (Exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
  discardClient(client);
  return nullptr;
}

}  // namespace spx
