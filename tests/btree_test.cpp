#include "bbtree/btree.hpp"

#include <algorithm>
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

template <std::size_t Degree>
void differential_test() {
  auto tree = bbtree::BTreeMap<std::uint64_t, std::uint64_t, Degree>{};
  auto reference = std::map<std::uint64_t, std::uint64_t>{};
  auto random = std::mt19937_64{12345};

  for (auto step = 0; step < 100000; ++step) {
    const auto key = random() % 2000;
    const auto value = random();
    switch (step % 4) {
      case 0:
      case 1: {
        const auto inserted = tree.insert_or_assign(key, value);
        const auto expected = reference.insert_or_assign(key, value).second;
        assert(inserted == expected);
        break;
      }
      case 2:
        assert(tree.erase(key) == (reference.erase(key) != 0));
        break;
      default: {
        const auto found = tree.get(key);
        const auto expected = reference.find(key);
        assert((found != nullptr) == (expected != reference.end()));
        if (found) assert(*found == expected->second);
      }
    }
    assert(tree.size() == reference.size());
    if (step % 1000 == 0) {
      auto entries = std::vector<std::pair<std::uint64_t, std::uint64_t>>{};
      tree.for_each([&](const auto& k, const auto& v) { entries.emplace_back(k, v); });
      assert(entries.size() == reference.size());
      assert(std::equal(entries.begin(), entries.end(), reference.begin()));

      const auto start = random() % 2000;
      const auto limit = static_cast<std::size_t>(random() % 40);
      entries.clear();
      tree.for_each_from(start, limit, [&](const auto& k, const auto& v) {
        entries.emplace_back(k, v);
      });
      auto expected_range = std::vector<std::pair<std::uint64_t, std::uint64_t>>{};
      for (auto it = reference.lower_bound(start);
           it != reference.end() && expected_range.size() < limit; ++it) {
        expected_range.push_back(*it);
      }
      assert(entries == expected_range);
    }
  }

  for (const auto& [key, value] : reference) {
    const auto found = tree.get(key);
    assert(found && *found == value);
    assert(tree.erase(key));
  }
  assert(tree.empty());
  assert(!tree.erase(0));

  for (std::uint64_t key = 0; key < 5000; ++key) {
    assert(tree.insert_or_assign(key, key + 1));
  }
  auto range = std::vector<std::uint64_t>{};
  tree.for_each_from(123, 7, [&](const auto& key, const auto&) {
    range.push_back(key);
  });
  assert((range == std::vector<std::uint64_t>{123, 124, 125, 126, 127, 128, 129}));
  range.clear();
  tree.for_each_from(4999, 7, [&](const auto& key, const auto&) {
    range.push_back(key);
  });
  assert((range == std::vector<std::uint64_t>{4999}));
  range.clear();
  tree.for_each_from(5000, 7, [&](const auto& key, const auto&) {
    range.push_back(key);
  });
  tree.for_each_from(0, 0, [&](const auto& key, const auto&) {
    range.push_back(key);
  });
  assert(range.empty());
  for (std::uint64_t key = 0; key < 5000; key += 2) {
    assert(tree.erase(key));
    assert(!tree.erase(key));
  }
  range.clear();
  tree.for_each_from(122, 3, [&](const auto& key, const auto&) {
    range.push_back(key);
  });
  assert((range == std::vector<std::uint64_t>{123, 125, 127}));
  for (auto key = 4999; key > 0; key -= 2) {
    assert(tree.erase(static_cast<std::uint64_t>(key)));
  }
  assert(tree.empty());
}

int main() {
  differential_test<2>();
  differential_test<16>();

  auto tree = bbtree::BTreeMap<std::string, std::string>{};
  assert(tree.insert_or_assign("a", "one"));
  assert(!tree.insert_or_assign("a", "two"));
  assert(*tree.get("a") == "two");
  assert(tree.erase("a"));
  assert(tree.get("a") == nullptr);
}
