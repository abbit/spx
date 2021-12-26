#pragma once

#include <string>

#include "exception.h"
#include "pthread.h"

namespace spx {

class MutexException : public Exception {
 public:
  explicit MutexException(const std::string& message) : Exception(message) {}
};

class Mutex {
 public:
  Mutex();
  ~Mutex();

  void lock();
  void unlock();

  Mutex(const Mutex& other) = delete;
  Mutex& operator=(const Mutex& other) = delete;

  pthread_mutex_t* getMutex();

 private:
  pthread_mutex_t mutex_{};
};

}  // namespace spx
