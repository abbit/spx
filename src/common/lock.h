#pragma once

#include "mutex.h"

namespace spx {

class Lock {
 public:
  Lock(Mutex &mtx) : mtx_(mtx) { mtx.lock(); }
  ~Lock() { mtx_.unlock(); }

 private:
  Mutex &mtx_;
};

}  // namespace spx
