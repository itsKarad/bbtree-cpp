#include "bbtree/btree.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

static constexpr std::string_view USAGE{"usage: bbtree_bench [entry_count] [repetitions]"};
static constexpr std::uint32_t KEY_MULTIPLIER{0x9e3779b1U};
static constexpr std::uint32_t QUERY_MULTIPLIER{0x85ebca6bU};

std::uint64_t key_at(std::uint32_t index) {
  return (static_cast<std::uint64_t>(index * KEY_MULTIPLIER) << 1U) | 1U;
}

template <class Map, class Insert, class Get>
void measure(std::string_view name, std::uint32_t count, int repetitions,
             Insert insert, Get get) {
  auto insert_times = std::vector<double>{};
  auto get_times = std::vector<double>{};
  std::uint64_t checksum = 0;
  for (auto repetition = 0; repetition < repetitions; ++repetition) {
    auto map = Map{};
    const auto insert_start = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < count; ++index) {
      insert(map, key_at(index), static_cast<std::uint64_t>(index));
    }
    const auto insert_end = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto query = (index * QUERY_MULTIPLIER) % count;
      const auto key = key_at(query);
      checksum += get(map, index & 1U ? key - 1 : key);
    }
    const auto get_end = std::chrono::steady_clock::now();
    const auto ns = [](auto start, auto end, std::uint32_t operations) {
      return std::chrono::duration<double, std::nano>(end - start).count() / operations;
    };
    insert_times.push_back(ns(insert_start, insert_end, count));
    get_times.push_back(ns(insert_end, get_end, count));
  }
  std::sort(insert_times.begin(), insert_times.end());
  std::sort(get_times.begin(), get_times.end());
  std::cout << name << " insert_ns=" << std::fixed << std::setprecision(1)
            << insert_times[repetitions / 2] << " get_ns="
            << get_times[repetitions / 2] << " checksum=" << checksum << '\n';
}

int main(int argc, char** argv) {
  if (argc > 3) {
    std::cerr << USAGE << '\n';
    return 1;
  }
  const auto count = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1000000UL;
  const auto repetitions = argc > 2 ? std::atoi(argv[2]) : 5;
  if (count == 0 || count > 10000000UL || repetitions < 1 || repetitions > 25) {
    std::cerr << USAGE << '\n';
    return 1;
  }

  const auto n = static_cast<std::uint32_t>(count);
  measure<bbtree::BTreeMap<std::uint64_t, std::uint64_t>>(
      "cpp_btree", n, repetitions,
      [](auto& map, auto key, auto value) { map.insert_or_assign(key, value); },
      [](const auto& map, auto key) { const auto* value = map.get(key); return value ? *value : 0ULL; });
  measure<std::map<std::uint64_t, std::uint64_t>>(
      "cpp_std_map", n, repetitions,
      [](auto& map, auto key, auto value) { map.insert_or_assign(key, value); },
      [](const auto& map, auto key) { const auto it = map.find(key); return it == map.end() ? 0ULL : it->second; });
}
