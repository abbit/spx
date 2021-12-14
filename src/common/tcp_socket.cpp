#include "tcp_socket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <utility>

#include "exception.h"

namespace spx {
bool TcpSocket::operator==(const TcpSocket &other) const {
  return getFileDescriptor() == other.getFileDescriptor();
}

bool TcpSocket::operator==(const int &other) const {
  return getFileDescriptor() == other;
}

std::ostream &operator<<(std::ostream &os, const TcpSocket &that) {
  os << "TcpSocket("
     << "fd=" << that.getFileDescriptor() << ")";
  return os;
}

bool TcpSocket::isValid() const {
  return getFileDescriptor() != INVALID_SOCKET_FD;
}

int TcpSocket::getFileDescriptor() const { return fd_; }

TcpSocket::TcpSocket(int domain, int type, int protocol) {
  fd_ = socket(domain, type, protocol);
  if (fd_ == INVALID_SOCKET_FD) {
    throw Exception("Failed to create tcp socket");
  }
  std::cout << "Created socket with fd " << fd_ << std::endl;
}

TcpSocket::TcpSocket(const addrinfo *const info)
    : TcpSocket(info->ai_family, info->ai_socktype, info->ai_protocol) {}

TcpSocket::TcpSocket() : TcpSocket(PF_INET, SOCK_STREAM, 0) {}

TcpSocket::TcpSocket(int fd) : fd_(fd) {
  std::cout << "Created socket with fd " << fd_ << std::endl;
}

TcpSocket::TcpSocket(TcpSocket &&other) noexcept { swap(other); }

TcpSocket::~TcpSocket() {
  if (isValid()) {
    int fd = getFileDescriptor();
    close(fd);
    std::cout << "Closed socket with fd " << fd << std::endl;
  }
}

TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept {
  swap(other);
  return *this;
}

void TcpSocket::swap(TcpSocket &other) noexcept {
  std::swap(this->fd_, other.fd_);
}
}  // namespace spx
