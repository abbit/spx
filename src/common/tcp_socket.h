#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "exception.h"

namespace spx {
const int INVALID_SOCKET_FD = -1;

class TcpSocket {
 public:
  TcpSocket(const TcpSocket &) = delete;

  TcpSocket &operator=(const TcpSocket &) = delete;

  bool operator==(const TcpSocket &other) const;

  bool operator==(const int &other) const;

  friend std::ostream &operator<<(std::ostream &os, const TcpSocket &that);

  bool isValid() const;

  int getFileDescriptor() const;

  void close();

 protected:
  TcpSocket();
  explicit TcpSocket(const addrinfo *info);
  explicit TcpSocket(int fd);
  TcpSocket(int domain, int type, int protocol);
  TcpSocket(TcpSocket &&other) noexcept;

  ~TcpSocket();

  TcpSocket &operator=(TcpSocket &&other) noexcept;

 private:
  int fd_{INVALID_SOCKET_FD};

  void swap(TcpSocket &other) noexcept;
};
}  // namespace spx
