#include "mutex.h"

#include <sstream>

namespace spx {

Mutex::Mutex() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  int err;
  if ((err = pthread_mutex_init(&mutex_, &attr)) != 0) {
    std::stringstream ss;
    ss << "Error on mutex_init(): " << std::strerror(err);
    throw MutexException(ss.str());
  }
  pthread_mutexattr_destroy(&attr);
}

Mutex::~Mutex() { pthread_mutex_destroy(&mutex_); }

void Mutex::lock() {
  int err;
  if ((err = pthread_mutex_lock(&mutex_)) != 0) {
    std::stringstream ss;
    ss << "Error on mutex_lock(): " << std::strerror(err);
    throw MutexException(ss.str());
  }
}

void Mutex::unlock() {
  int err;
  if ((err = pthread_mutex_unlock(&mutex_)) != 0) {
    std::stringstream ss;
    ss << "Error on mutex_unlock(): " << std::strerror(err);
    throw MutexException(ss.str());
  }
}

pthread_mutex_t *Mutex::getMutex() { return &mutex_; }

}  // namespace spx
