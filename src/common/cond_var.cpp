#include "cond_var.h"

#include <sstream>

namespace spx {

CondVar::CondVar() {
  int err;
  if ((err = pthread_cond_init(&cond_, nullptr)) != 0) {
    std::stringstream ss;
    ss << "Error on cond_init(): " << std::strerror(err);
    throw CondVarException(ss.str());
  }
}

CondVar::~CondVar() { pthread_cond_destroy(&cond_); }

void CondVar::wait(Mutex &mtx) {
  int err;
  if ((err = pthread_cond_wait(&cond_, mtx.getMutex())) != 0) {
    std::stringstream ss;
    ss << "Error on cond_wait(): " << std::strerror(err);
    throw CondVarException(ss.str());
  }
}

void CondVar::notify() {
  int err;
  if ((err = pthread_cond_signal(&cond_)) != 0) {
    std::stringstream ss;
    ss << "Error on cond_signal(): " << std::strerror(err);
    throw CondVarException(ss.str());
  }
}

void CondVar::notifyAll() {
  int err;
  if ((err = pthread_cond_broadcast(&cond_)) != 0) {
    std::stringstream ss;
    ss << "Error on cond_broadcast(): " << std::strerror(err);
    throw CondVarException(ss.str());
  }
}

}  // namespace spx
