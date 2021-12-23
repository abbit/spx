#pragma once

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "exception.h"

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
  class Entry {
   public:
    size_t size() const { return buffer.size(); }
    const char *data() const { return buffer.data(); }
    void append(const char *buf, size_t len) {
      buffer.insert(buffer.end(), buf, buf + len);
    }

    bool isCompleted() const { return completed; }
    void complete() { completed = true; }

    int inUse() const { return in_use; }
    void incrementUse() { in_use++; }
    void decrementUse() { in_use--; }

   private:
    std::vector<char> buffer;
    bool completed{false};
    int in_use{1};
  };

  static std::unique_ptr<Cache> create(size_t max_bytes);

  void write(const std::string &key, const char *buf, size_t buf_len);
  std::vector<char> read(const std::string &key, size_t offset, size_t len);

  bool contains(const std::string &key);
  Entry &getEntry(const std::string &key);

  void useEntry(const std::string &key);
  void disuseEntry(const std::string &key);

 private:
  using List = std::list<std::pair<std::string, Entry>>;

  size_t max_size_;
  size_t current_size_;
  List list_;
  std::unordered_map<std::string, List::iterator> hash_table_;

  explicit Cache(size_t max_bytes);

  void removeLeastRecentlyUsed();
  void makeMostRecentlyUsed(const std::string &key);

  void append(const std::string &key, const char *buf, size_t buf_len);

  void writeToFront(const char *buf, size_t buf_len);

  void drop(const std::string &key);

  void freeSpace(size_t needed_space);
  size_t getFreeSpace() const;
};

}  // namespace spx
