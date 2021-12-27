#include "cache.h"

#include <iostream>

#include "exception.h"
#include "lock.h"

namespace spx {

std::unique_ptr<Cache> Cache::create(size_t max_items) {
  return std::unique_ptr<Cache>(new Cache(max_items));
}

Cache::Cache(size_t max_bytes) : max_size_(max_bytes), current_size_(0) {}

void Cache::write(const std::string &key, const char *buf, size_t buf_len) {
  freeSpace(buf_len);
  current_size_ += buf_len;
  auto &entry = getEntry(key);
  entry.append(buf, buf_len);
  entry.mutex.unlock();
  entry.cond_var.notifyAll();

  std::cout << "Write cache entry data(len=" << buf_len << "), key:\n"
            << key << std::endl;
}

std::vector<char> Cache::read(const std::string &key, size_t offset,
                              size_t len) {
  auto &entry = getEntry(key);
  std::vector<char> res{entry.data() + offset, entry.data() + offset + len};

  std::cout << "Read cache entry data(entry size=" << entry.size()
            << ", read offset=" << offset << ", read len=" << len << "), key:\n"
            << key << std::endl;
  entry.mutex.unlock();

  return res;
}

void Cache::removeLeastRecentlyUsed() {
  std::cout << "Remove least recently used" << std::endl;
  // find first unused from least used to recently used
  auto cache_lock = Lock(cache_mutex_);

  auto to_be_removed = list_.end();

  for (auto it = list_.begin(); it != list_.end(); ++it) {
    if (!it->second->in_use) {
      to_be_removed = it;
      break;
    }
  }

  if (to_be_removed != list_.end()) {
    drop(to_be_removed->first);
  } else {
    throw AllInUseException();
  }
}

Cache::Entry &Cache::getEntry(const std::string &key) {
  auto cache_lock = Lock(cache_mutex_);

  try {
    list_.splice(list_.end(), list_, hash_table_.at(key));
    hash_table_[key] = std::prev(list_.end());
  } catch (const std::out_of_range &e) {
    throw KeyNotFoundException();
  }

  auto &entry = *list_.back().second;
  entry.mutex.lock();
  return entry;
}

bool Cache::contains(const std::string &key) {
  return hash_table_.find(key) != hash_table_.end();
}

void Cache::drop(const std::string &key) {
  if (!contains(key)) {
    return;
  }

  auto &entry = hash_table_[key]->second;
  current_size_ -= entry->size();
  list_.erase(hash_table_[key]);
  hash_table_.erase(key);
  std::cout << "Dropped cache with key:\n" << key << std::endl;
}

size_t Cache::getFreeSpace() const { return max_size_ - current_size_; }

void Cache::freeSpace(size_t needed_space) {
  while (getFreeSpace() < needed_space) {
    removeLeastRecentlyUsed();
  }
}
void Cache::useEntry(const std::string &key) {
  if (contains(key)) {
    auto &entry = getEntry(key);
    entry.incrementUse();
    entry.mutex.unlock();
  } else {
    auto cache_lock = Lock(cache_mutex_);

    list_.emplace_back(key, std::make_unique<Entry>());
    hash_table_[key] = std::prev(list_.end());
    std::cout << "first use of entry, key:\n" << key << std::endl;
  }
}

bool Cache::useEntryIfExists(const std::string &key) {
  bool used = false;
  if (contains(key)) {
    auto &entry = getEntry(key);
    entry.incrementUse();
    entry.mutex.unlock();
    used = true;
  }

  return used;
}

void Cache::disuseEntry(const std::string &key) {
  if (!contains(key)) return;

  auto &entry = getEntry(key);
  entry.decrementUse();
  std::cout << "cache entry disused, current in_use=" << entry.in_use
            << ",completed=" << entry.completed << ",key:\n"
            << key << std::endl;
  if (!entry.in_use && !entry.completed) {
    auto cache_lock = Lock(cache_mutex_);

    drop(key);
  }

  entry.mutex.unlock();
}
bool Cache::isEntryCompleted(const std::string &key) {
  auto &entry = getEntry(key);
  bool completed = entry.completed;
  entry.mutex.unlock();
  return completed;
}

void Cache::completeEntry(const std::string &key) {
  auto &entry = getEntry(key);
  entry.completed = true;
  entry.mutex.unlock();
  entry.cond_var.notifyAll();
}

void Cache::waitForEntryUpdate(const std::string &key) {
  auto &entry = getEntry(key);
  if (!entry.completed) {
    entry.cond_var.wait(entry.mutex);
  }
  entry.mutex.unlock();
}

void Cache::notify(const std::string &key) {
  auto &entry = getEntry(key);
  entry.cond_var.notifyAll();
  entry.mutex.unlock();
}

size_t Cache::getEntrySize(const std::string &key) {
  auto &entry = getEntry(key);
  size_t size = entry.size();
  entry.mutex.unlock();
  return size;
}

}  // namespace spx