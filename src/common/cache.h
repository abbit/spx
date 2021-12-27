#pragma once

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cond_var.h"
#include "exception.h"
#include "mutex.h"

namespace spx {

class AllInUseException : public Exception {
 public:
  AllInUseException() : Exception("all cache entries are in use") {}
};

class KeyNotFoundException : public Exception {
 public:
  KeyNotFoundException() : Exception("key not found") {}
};

class Cache {
 public:
  static std::unique_ptr<Cache> create(size_t max_bytes);

  void write(const std::string &key, const char *buf, size_t buf_len);

  std::vector<char> read(const std::string &key, size_t offset, size_t len);

  bool contains(const std::string &key);

  void useEntry(const std::string &key);
  bool useEntryIfExists(const std::string &key);
  void disuseEntry(const std::string &key);

  bool isEntryCompleted(const std::string &key);
  void completeEntry(const std::string &key);
  size_t getEntrySize(const std::string &key);

  void waitForEntryUpdate(const std::string &key);
  void notify(const std::string &key);

  Mutex cache_mutex_;

 private:
  struct Entry {
    std::vector<char> buffer;
    bool completed{false};
    Mutex mutex;
    CondVar cond_var;
    int in_use{1};

    size_t size() const { return buffer.size(); }
    const char *data() const { return buffer.data(); }

    void append(const char *buf, size_t len) {
      buffer.insert(buffer.end(), buf, buf + len);
    }

    void incrementUse() { in_use++; }
    void decrementUse() { in_use--; }
  };

  using List = std::list<std::pair<std::string, std::unique_ptr<Entry>>>;

  size_t max_size_;
  size_t current_size_;
  List list_;
  std::unordered_map<std::string, List::iterator> hash_table_;

  explicit Cache(size_t max_bytes);

  Entry &getEntry(const std::string &key);

  void removeLeastRecentlyUsed();

  void drop(const std::string &key);

  void freeSpace(size_t needed_space);
  size_t getFreeSpace() const;
};

}  // namespace spx
