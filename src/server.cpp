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
const size_t MAX_CHUNK_SIZE = 64 * 1024;      // 64 kb

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

}  // namespace

bool Server::is_running_ = false;
auto Server::cache_ = Cache::create(MAX_CACHE_SIZE);
// std::unordered_map<std::string, RequestClientsMapEntry>
//     Server::request_clients_map{};
// Mutex Server::request_clients_map_mutex_;

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

// bool Server::hasClientsWithRequest(const std::string &request) {
//   auto clients_list = request_clients_map.find(request);
//   bool res = false;
//   if (clients_list != request_clients_map.end()) {
//     res = !clients_list->second.list.empty();
//   }
//
//   return res;
// }

// void Server::addToRequestClients(Client &client) {
//   auto &entry = getRequestClientsMapEntry(client.request_str);
//   //  entry.mutex.lock();
//   entry.list.emplace_back(client);
//   //  entry.mutex.unlock();
// }

// void Server::removeFromRequestClients(Client &client) {
//   auto &entry = getRequestClientsMapEntry(client.request_str);
//   entry.mutex->lock();
//   entry.list.erase(std::remove_if(entry.list.begin(), entry.list.end(),
//                                   [&client](std::reference_wrapper<Client>
//                                   &c) {
//                                     return client == c;
//                                   }),
//                    entry.list.end());
//   entry.mutex->unlock();
// }

void Server::prepareForSendingRequest(Client &client) {
  client.server_connection =
      connectToHost(client.request.url.hostname(),
                    std::to_string(client.request.url.httpPort()));
  client.state = Client::State::sending_request;
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

  cache_->disuseEntry(client->request_str);
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

void Server::setClientToGetResponseFromCache(Client &client) {
  client.state = Client::State::get_from_cache;
  cache_->useEntry(client.request_str);
  client.should_use_cache = true;
}

void Server::readRequestFromClient(Client &client) {
  std::cout << client << "Read request from client" << std::endl;

  client.getHttpRequest();
  std::cout << client.request_str << std::endl;
  // if response to request already in cache, then just use it
  // otherwise send request to server
  //  auto &entry = getRequestClientsMapEntry(client.request_str);
  //  entry.mutex->lock();
  if (cache_->contains(client.request_str)) {
    std::cout << "Have response in cache!" << std::endl;
    setClientToGetResponseFromCache(client);
    //  } else if (hasClientsWithRequest(client.request_str)) {
    //    std::cout << "Has another client with this request, wait for his
    //    response "
    //              << std::endl;
    //    client.state = Client::State::waiting_for_response_for_another_client;
  } else {
    std::cout << "Response is not in cache" << std::endl;
    prepareForSendingRequest(client);
  }
  //  addToRequestClients(client);
  //  entry.mutex->unlock();
}

void Server::sendRequestToServer(Client &client) {
  std::cout << client << "Send request to server" << std::endl;

  ActiveSocket &server_conn = *client.server_connection;
  server_conn.send(client.request_str);
  server_conn.shutdownWrite();
}

// void Server::prepareAllWaitingClientsForSending(const std::string &request) {
//   auto &entry = getRequestClientsMapEntry(request);
//   entry.mutex->lock();
//   for (auto &c : entry.list) {
//     if (c.get().state ==
//         Client::State::waiting_for_response_for_another_client) {
//       prepareForSendingRequest(c.get());
//     }
//   }
//   entry.mutex->unlock();
// }

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
      client.should_use_cache = true;
      cache_->useEntry(client.request_str);
      writeResponseChunkToCache(client, chunk);
      //          auto &entry = getRequestClientsMapEntry(client.request_str);
      //          entry.mutex->lock();
      //          // TODO
      //          for (auto &c : entry.list) {
      //            if (client != c) {
      //              setClientToGetResponseFromCache(c.get());
      //            }
      //          }
      //          entry.mutex->unlock();
    } else {
      //          prepareAllWaitingClientsForSending(client.request_str);
    }

    //    entry.cond_var->notifyAll();

    client.sendToClient(chunk);
  } catch (const Exception &e) {
    //    prepareAllWaitingClientsForSending(client.request_str);
  }
}

void Server::transferResponse(Client &client) {
  std::cout << client << "Transfer response chunk" << std::endl;

  try {
    std::vector<char> chunk = readResponseChunk(client);

    if (client.should_use_cache) {
      writeResponseChunkToCache(client, chunk);
    }

    client.sendToClient(chunk);
  } catch (const ZeroLengthMessageException &e) {
    std::cout << "Done transferring response" << std::endl;
    if (client.should_use_cache) {
      cache_->completeEntry(client.request_str);
    }
    client.done = true;
  }
}

void Server::sendCachedResponseToClient(Client &client) {
  std::cout << client << "Send cached response" << std::endl;

  if (cache_->isEntryCompleted(client.request_str)) {
    client.sendToClient(cache_->readAll(client.request_str));
    client.done = true;
  } else {
    throw Exception("cache isn't complete");

    ////     TODO: change to notify system
    //    const size_t received_bytes = cache_entry.size();
    //    const bool is_new_data_available = client.sent_bytes < received_bytes;
    //    if (!is_new_data_available) {
    //      if (cache_entry.isCompleted()) {
    //        std::cout << "DONE!!!" << std::endl;
    //      } else {
    //        //        std::cout << "No new data" << std::endl;
    //      }
    //      return;
    //    }
    //    size_t chunk_size =
    //        std::min(received_bytes - client.sent_bytes, MAX_CHUNK_SIZE);
    //    client.sendToClient(
    //        cache_->read(client.request_str, client.sent_bytes, chunk_size));
  }
}

void *Server::handleClient(void *arg) {
  auto *client = reinterpret_cast<Client *>(arg);

  try {
    std::cout << "Handling " << *client << std::endl;

    while (!client->done) {
      std::cout << "checking state" << std::endl;
      switch (client->state) {
        case Client::State::initial: {
          readRequestFromClient(*client);
          break;
        }
          //      case Client::State::waiting_for_response_for_another_client: {
          //        auto &entry =
          //        getRequestClientsMapEntry(client->request_str);
          //        entry.cond_var->wait();
          //        break;
          //      }
        case Client::State::sending_request: {
          sendRequestToServer(*client);
          readResponseStatusCodeFromServer(*client);
          while (!client->done) {
            transferResponse(*client);
          }
          break;
        }
        case Client::State::get_from_cache: {
          sendCachedResponseToClient(*client);
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

// RequestClientsMapEntry &Server::getRequestClientsMapEntry(
//     const std::string &request) {
//   request_clients_map_mutex_.lock();
//   if (request_clients_map.find(request) == request_clients_map.end()) {
//     request_clients_map[request] = {
//         {}, std::make_unique<Mutex>(), std::make_unique<CondVar>()};
//   }
//   RequestClientsMapEntry &res = request_clients_map.at(request);
//   request_clients_map_mutex_.unlock();
//   return res;
// }

}  // namespace spx
