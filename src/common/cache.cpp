#include "cache.h"

#include <iostream>

#include "exception.h"

namespace spx {

std::unique_ptr<Cache> Cache::create(size_t max_items) {
  return std::unique_ptr<Cache>(new Cache(max_items));
}

Cache::Cache(size_t max_bytes) : max_size_(max_bytes), current_size_(0) {}

void Cache::write(const std::string &key, const char *buf, size_t buf_len) {
  freeSpace(buf_len);
  current_size_ += buf_len;
  append(key, buf, buf_len);

  std::cout << "Write cache entry data(len=" << buf_len << "), key:\n"
            << key << std::endl;
  std::cout << "Space used: " << current_size_ << std::endl;
}

std::vector<char> Cache::read(const std::string &key, size_t offset,
                              size_t len) {
  makeMostRecentlyUsed(key);
  auto &entry = list_.back().second;
  std::cout << "Read cache entry data(size=" << entry.size()
            << ", offset=" << offset << ", len=" << len << "), key:\n"
            << key << std::endl;
  return {entry.data() + offset, entry.data() + offset + len};
}

Cache::Entry &Cache::getEntry(const std::string &key) {
  makeMostRecentlyUsed(key);
  return hash_table_.at(key)->second;
}

void Cache::removeLeastRecentlyUsed() {
  std::cout << "Remove least recently used" << std::endl;
  // find first unused from least used to recently used
  auto to_be_removed = list_.end();

  for (auto it = list_.begin(); it != list_.end(); ++it) {
    if (!it->second.inUse()) {
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

void Cache::makeMostRecentlyUsed(const std::string &key) {
  try {
    list_.splice(list_.end(), list_, hash_table_.at(key));
    hash_table_[key] = std::prev(list_.end());
  } catch (const std::out_of_range &e) {
    throw KeyNotFoundException();
  }
}

void Cache::append(const std::string &key, const char *buf, size_t buf_len) {
  makeMostRecentlyUsed(key);
  writeToFront(buf, buf_len);
  std::cout << "appended" << std::endl;
}

void Cache::writeToFront(const char *buf, size_t buf_len) {
  auto &entry = list_.back().second;
  entry.append(buf, buf_len);
}

bool Cache::contains(const std::string &key) {
  return hash_table_.find(key) != hash_table_.end();
}

void Cache::drop(const std::string &key) {
  if (!contains(key)) return;

  current_size_ -= hash_table_[key]->second.size();
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
    getEntry(key).incrementUse();
  } else {
    list_.push_back({key, {}});
    hash_table_[key] = std::prev(list_.end());
    std::cout << "first use of entry, key:\n" << key << std::endl;
  }
}

void Cache::disuseEntry(const std::string &key) {
  if (!contains(key)) return;

  auto &entry = getEntry(key);
  entry.decrementUse();
  std::cout << "cache entry disused, current in_use=" << entry.inUse()
            << ",key:\n"
            << key << std::endl;
  if (!entry.inUse() && !entry.isCompleted()) {
    drop(key);
  }
}

}  // namespace spx
