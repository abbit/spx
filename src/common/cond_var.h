#pragma once

#include <string>

#include "mutex.h"

namespace spx {

class CondVarException : public Exception {
 public:
  explicit CondVarException(const std::string &message) : Exception(message) {}
};

class CondVar {
 public:
  CondVar();
  ~CondVar();

  CondVar(const CondVar &other) = delete;
  CondVar &operator=(const CondVar &other) = delete;

  void wait(Mutex &mtx);
  void notify();
  void notifyAll();

 private:
  pthread_cond_t cond_{};
};

}  // namespace spx
