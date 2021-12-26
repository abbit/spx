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
  makeMostRecentlyUsed(key);
  auto &entry = list_.back().second;
  entry->append(buf, buf_len);
  std::cout << "appended" << std::endl;

  std::cout << "Write cache entry data(len=" << buf_len << "), key:\n"
            << key << std::endl;
  std::cout << "Space used: " << current_size_ << std::endl;
}

std::vector<char> Cache::read(const std::string &key, size_t offset,
                              size_t len) {
  makeMostRecentlyUsed(key);
  auto &entry = list_.back().second;

  std::vector<char> res{entry->data() + offset, entry->data() + offset + len};

  std::cout << "Read cache entry data(size=" << entry->size()
            << ", offset=" << offset << ", len=" << len << "), key:\n"
            << key << std::endl;

  return res;
}

std::vector<char> Cache::readAll(const std::string &key) {
  auto &entry = getEntry(key);

  return read(key, 0, entry.size());
}

Cache::Entry &Cache::getEntry(const std::string &key) {
  makeMostRecentlyUsed(key);
  Cache::Entry &res = *hash_table_.at(key)->second;

  return res;
}

void Cache::removeLeastRecentlyUsed() {
  std::cout << "Remove least recently used" << std::endl;
  // find first unused from least used to recently used
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

void Cache::makeMostRecentlyUsed(const std::string &key) {
  try {
    list_.splice(list_.end(), list_, hash_table_.at(key));
    hash_table_[key] = std::prev(list_.end());
  } catch (const std::out_of_range &e) {
    throw KeyNotFoundException();
  }
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
    getEntry(key).incrementUse();
  } else {
    list_.emplace_back(key, std::make_unique<Entry>());
    hash_table_[key] = std::prev(list_.end());
    std::cout << "first use of entry, key:\n" << key << std::endl;
  }
}

void Cache::disuseEntry(const std::string &key) {
  if (!contains(key)) return;

  auto &entry = getEntry(key);
  entry.decrementUse();
  std::cout << "cache entry disused, current in_use=" << entry.in_use
            << ",key:\n"
            << key << std::endl;
  if (!entry.in_use && !entry.completed) {
    drop(key);
  }
}
bool Cache::isEntryCompleted(const std::string &key) {
  return getEntry(key).completed;
}

void Cache::completeEntry(const std::string &key) {
  getEntry(key).completed = true;
}

}  // namespace spx