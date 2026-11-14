#include "bbtree/btree.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr std::string_view USAGE{
    "usage: bbtree_bench [entry_count] [duration_ms] "
    "[--keys u64|uuid|str-json|wiki-titles] [--keys-file path]"};
static constexpr std::uint64_t DEFAULT_ENTRY_COUNT{10000000};
static constexpr std::uint64_t MAX_ENTRY_COUNT{10000000};
static constexpr std::uint64_t MAX_ABSENT_KEYS{1U << 20U};
static constexpr std::size_t QUERIES_PER_WORKER{1U << 18U};
static constexpr std::size_t OPERATION_BATCH{256};
static constexpr std::size_t SCAN_LENGTH{100};
static constexpr std::uint64_t SPLITMIX_GAMMA{0x9e3779b97f4a7c15ULL};

enum class KeyKind { U64, Uuid, JsonStrings, WikiTitles };

struct Options {
  std::optional<std::uint64_t> entries;
  std::uint64_t duration_ms{1000};
  KeyKind key_kind{KeyKind::U64};
  bool key_kind_specified{false};
  std::string key_file;
};

struct KeyFile {
  std::vector<std::string> present;
  std::vector<std::string> absent;
};

struct TimeResult {
  std::uint64_t operations{0};
  std::uint64_t checksum{0};
  std::chrono::nanoseconds elapsed{};
};

template <class Key>
struct BenchmarkData {
  std::vector<Key> present;
  std::vector<Key> get_queries;
  std::vector<Key> present_queries;
};

std::uint64_t mix(std::uint64_t value) {
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::uint64_t multiply_high(std::uint64_t left, std::uint64_t right) {
  constexpr std::uint64_t LOW_MASK{0xffffffffULL};
  const auto left_low = left & LOW_MASK;
  const auto left_high = left >> 32U;
  const auto right_low = right & LOW_MASK;
  const auto right_high = right >> 32U;
  const auto low_product = left_low * right_low;
  const auto middle_left = left_high * right_low;
  const auto middle_right = left_low * right_high;
  const auto carry = (low_product >> 32U) + (middle_left & LOW_MASK) +
                     (middle_right & LOW_MASK);
  return left_high * right_high + (middle_left >> 32U) +
         (middle_right >> 32U) + (carry >> 32U);
}

class IndexStream {
 public:
  IndexStream(std::uint64_t worker, std::uint64_t round)
      : state_(mix((round << 32U) ^ worker ^ 0xc0ffeeULL)) {}

  std::size_t next_below(std::size_t bound) {
    state_ += SPLITMIX_GAMMA;
    return static_cast<std::size_t>(multiply_high(mix(state_), bound));
  }

 private:
  std::uint64_t state_;
};

bool parse_unsigned(std::string_view text, std::uint64_t& value) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const auto parsed = std::strtoull(text.data(), &end, 10);
  if (errno != 0 || end == text.data() || *end != '\0') return false;
  value = static_cast<std::uint64_t>(parsed);
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
  auto positionals = std::vector<std::string_view>{};
  for (auto index = 1; index < argc; ++index) {
    const auto arg = std::string_view{argv[index]};
    if (arg == "--keys" || arg == "--keys-file") {
      if (index + 1 >= argc) return false;
      const auto value = std::string_view{argv[++index]};
      if (arg == "--keys") {
        options.key_kind_specified = true;
        if (value == "u64") options.key_kind = KeyKind::U64;
        else if (value == "uuid") options.key_kind = KeyKind::Uuid;
        else if (value == "str-json") options.key_kind = KeyKind::JsonStrings;
        else if (value == "wiki-titles") options.key_kind = KeyKind::WikiTitles;
        else return false;
      } else {
        options.key_file = std::string{value};
      }
    } else if (!arg.empty() && arg.front() == '-') {
      return false;
    } else {
      positionals.push_back(arg);
    }
  }
  if (positionals.size() > 2) return false;
  if (!positionals.empty()) {
    std::uint64_t value = 0;
    if (!parse_unsigned(positionals[0], value)) return false;
    options.entries = value;
  }
  if (positionals.size() == 2 &&
      !parse_unsigned(positionals[1], options.duration_ms)) {
    return false;
  }
  if (!options.key_file.empty() && options.key_kind_specified &&
      options.key_kind != KeyKind::JsonStrings &&
      options.key_kind != KeyKind::WikiTitles) {
    return false;
  }
  if (options.key_file.empty() &&
      (options.key_kind == KeyKind::JsonStrings ||
       options.key_kind == KeyKind::WikiTitles)) {
    return false;
  }
  return true;
}

bool read_key_file(const std::string& path, KeyFile& keys, std::string& error) {
  auto input = std::ifstream{path};
  if (!input) {
    error = "could not open key file: " + path;
    return false;
  }
  auto header = std::string{};
  if (!std::getline(input, header)) {
    error = "key file is missing its count header";
    return false;
  }
  std::uint64_t present_count = 0;
  std::uint64_t absent_count = 0;
  auto counts = std::istringstream{header};
  if (!(counts >> present_count >> absent_count)) {
    error = "invalid key file count header";
    return false;
  }
  counts >> std::ws;
  if (!counts.eof() || present_count == 0 || present_count > MAX_ENTRY_COUNT ||
      absent_count == 0 ||
      absent_count > std::min(present_count, MAX_ABSENT_KEYS)) {
    error = "invalid key file count header";
    return false;
  }
  keys.present.reserve(static_cast<std::size_t>(present_count));
  keys.absent.reserve(static_cast<std::size_t>(absent_count));
  auto line = std::string{};
  for (std::uint64_t index = 0; index < present_count + absent_count; ++index) {
    if (!std::getline(input, line)) {
      error = "key file ended before all keys were read";
      return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    (index < present_count ? keys.present : keys.absent).push_back(line);
  }
  if (std::getline(input, line)) {
    error = "key file has extra lines after its declared keys";
    return false;
  }
  return true;
}

std::array<std::uint8_t, 16> uuid_key(std::uint64_t index) {
  auto key = std::array<std::uint8_t, 16>{};
  const auto high = mix(index);
  const auto low = mix(~index);
  for (auto byte = 0U; byte < 8U; ++byte) {
    key[byte] = static_cast<std::uint8_t>(high >> (56U - 8U * byte));
    key[8U + byte] = static_cast<std::uint8_t>(low >> (56U - 8U * byte));
  }
  key[6] = static_cast<std::uint8_t>((key[6] & 0x0fU) | 0x40U);
  key[8] = static_cast<std::uint8_t>((key[8] & 0x3fU) | 0x80U);
  return key;
}

template <class Key>
std::vector<Key> make_queries(const std::vector<Key>& pool) {
  auto rng = IndexStream{0, 1};
  auto queries = std::vector<Key>{};
  queries.reserve(QUERIES_PER_WORKER);
  for (auto index = 0U; index < QUERIES_PER_WORKER; ++index) {
    queries.push_back(pool[rng.next_below(pool.size())]);
  }
  return queries;
}

template <class Key>
BenchmarkData<Key> prepare_data(std::vector<Key> present,
                                const std::vector<Key>& absent) {
  const auto half_count = std::min(present.size(), absent.size());
  auto half_pool = std::vector<Key>{};
  half_pool.reserve(half_count * 2);
  half_pool.insert(half_pool.end(), present.begin(), present.begin() + half_count);
  half_pool.insert(half_pool.end(), absent.begin(), absent.begin() + half_count);
  auto get_queries = make_queries(half_pool);
  auto present_queries = make_queries(present);
  return {std::move(present), std::move(get_queries),
          std::move(present_queries)};
}

template <class Key, class Generator>
BenchmarkData<Key> make_data(std::size_t entries, Generator make_key) {
  const auto absent_count = std::min<std::size_t>(entries, MAX_ABSENT_KEYS);
  auto present = std::vector<Key>{};
  auto absent = std::vector<Key>{};
  present.reserve(entries);
  absent.reserve(absent_count);
  for (std::uint64_t index = 0; index < entries + absent_count; ++index) {
    auto key = make_key(index);
    if (index < entries) present.push_back(std::move(key));
    else absent.push_back(std::move(key));
  }
  return prepare_data(std::move(present), absent);
}

template <class Key>
BenchmarkData<Key> make_file_data(KeyFile keys) {
  return prepare_data(std::move(keys.present), keys.absent);
}

template <class Map, class Key>
void populate(Map& map, const std::vector<Key>& keys) {
  for (std::size_t index = 0; index < keys.size(); ++index) {
    map.insert_or_assign(keys[index], static_cast<std::uint64_t>(index));
  }
}

template <class Key, class Operation>
TimeResult time_queries(const std::vector<Key>& queries,
                        std::uint64_t duration_ms, Operation operation) {
  static_assert((QUERIES_PER_WORKER & (QUERIES_PER_WORKER - 1)) == 0,
                "query count must be a power of two");
  auto result = TimeResult{};
  auto query_index = std::size_t{0};
  const auto start = std::chrono::steady_clock::now();
  const auto target = std::chrono::milliseconds{duration_ms};
  do {
    for (auto index = 0U; index < OPERATION_BATCH; ++index) {
      result.checksum += operation(queries[query_index]);
      query_index = (query_index + 1U) & (QUERIES_PER_WORKER - 1U);
    }
    result.operations += OPERATION_BATCH;
  } while (std::chrono::steady_clock::now() - start < target);
  result.elapsed = std::chrono::steady_clock::now() - start;
  return result;
}

void print_time(std::string_view map_name, std::string_view workload,
                const TimeResult& result) {
  const auto ns_per_operation =
      std::chrono::duration<double, std::nano>(result.elapsed).count() /
      static_cast<double>(result.operations);
  std::cout << map_name << ' ' << workload << " ops=" << result.operations
            << " elapsed_ms=" << std::fixed << std::setprecision(1)
            << std::chrono::duration<double, std::milli>(result.elapsed).count()
            << " ns_per_op=" << std::setprecision(2) << ns_per_operation
            << " checksum=" << result.checksum << '\n';
}

template <class Map, class Key, class Lookup, class Mutate, class Scan>
void run_map(std::string_view name, const BenchmarkData<Key>& data,
             std::uint64_t duration_ms, Lookup lookup, Mutate mutate,
             Scan scan) {
  {
    auto map = Map{};
    populate(map, data.present);

    auto hits = std::uint64_t{0};
    auto get_result = time_queries(data.get_queries, duration_ms, [&](const Key& key) {
      const auto* value = lookup(map, key);
      if (!value) return std::uint64_t{0};
      ++hits;
      return *value;
    });
    print_time(name, "get_half", get_result);
    std::cout << "  get_half_hits=" << hits << '/' << get_result.operations << '\n';

    auto update_result = time_queries(data.present_queries, duration_ms,
                                      [&](const Key& key) {
                                        auto* value = mutate(map, key);
                                        if (value) ++*value;
                                        return std::uint64_t{0};
                                      });
    print_time(name, "update", update_result);

    auto short_scans = std::uint64_t{0};
    auto scan_result = time_queries(data.present_queries, duration_ms,
                                    [&](const Key& key) {
                                      auto visited = std::size_t{0};
                                      auto sum = std::uint64_t{0};
                                      scan(map, key, [&](const Key&, const std::uint64_t& value) {
                                        ++visited;
                                        sum += value;
                                      });
                                      if (visited < SCAN_LENGTH) ++short_scans;
                                      return sum;
                                    });
    print_time(name, "scan100", scan_result);
    std::cout << "  short_scans=" << short_scans << '/' << scan_result.operations
              << '\n';
  }

  auto insert_map = Map{};
  auto insert_keys = data.present;
  const auto insert_start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < insert_keys.size(); ++index) {
    insert_map.insert_or_assign(std::move(insert_keys[index]),
                                static_cast<std::uint64_t>(index));
  }
  const auto insert_elapsed = std::chrono::steady_clock::now() - insert_start;
  const auto insert_ns =
      std::chrono::duration<double, std::nano>(insert_elapsed).count() /
      static_cast<double>(data.present.size());
  std::cout << name << " insert ops=" << insert_keys.size() << " elapsed_ms="
            << std::fixed << std::setprecision(1)
            << std::chrono::duration<double, std::milli>(insert_elapsed).count()
            << " ns_per_op=" << std::setprecision(2) << insert_ns << '\n';
}

template <class Key>
void run_maps(std::string_view key_label, const BenchmarkData<Key>& data,
              std::uint64_t duration_ms) {
  std::cout << "key_kind=" << key_label << " entries=" << data.present.size()
            << " duration_ms=" << duration_ms
            << " timed_threads=1 setup_order=serial\n";
  std::cout << "unsupported get_half_batch: requires native get_many_copied(64)\n"
               "unsupported insert_batch: requires native insert_many(16)\n";

  run_map<bbtree::BTreeMap<Key, std::uint64_t>>(
      "cpp_btree", data, duration_ms,
      [](const auto& map, const Key& key) { return map.get(key); },
      [](auto& map, const Key& key) { return map.get_mut(key); },
      [](const auto& map, const Key& key, auto&& visitor) {
        map.for_each_from(key, SCAN_LENGTH,
                          std::forward<decltype(visitor)>(visitor));
      });

  run_map<std::map<Key, std::uint64_t>>(
      "cpp_std_map", data, duration_ms,
      [](const auto& map, const Key& key) {
        const auto found = map.find(key);
        return found == map.end() ? nullptr : &found->second;
      },
      [](auto& map, const Key& key) {
        const auto found = map.find(key);
        return found == map.end() ? nullptr : &found->second;
      },
      [](const auto& map, const Key& key, auto&& visitor) {
        auto found = map.lower_bound(key);
        for (auto index = std::size_t{0};
             index < SCAN_LENGTH && found != map.end(); ++index, ++found) {
          visitor(found->first, found->second);
        }
      });
}

int main(int argc, char** argv) {
  auto options = Options{};
  if (!parse_options(argc, argv, options)) {
    std::cerr << USAGE << '\n';
    return 1;
  }

  auto key_file = KeyFile{};
  auto entries = options.entries.value_or(DEFAULT_ENTRY_COUNT);
  if (!options.key_file.empty()) {
    auto error = std::string{};
    if (!read_key_file(options.key_file, key_file, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    if (options.entries && *options.entries != key_file.present.size()) {
      std::cerr << "entry_count does not match key file header\n";
      return 1;
    }
    entries = key_file.present.size();
  }
  if (entries == 0 || entries > MAX_ENTRY_COUNT || options.duration_ms == 0 ||
      options.duration_ms > 60000) {
    std::cerr << USAGE << '\n';
    return 1;
  }

  if (!options.key_file.empty()) {
    auto data = make_file_data<std::string>(std::move(key_file));
    const auto key_label = options.key_kind == KeyKind::JsonStrings
                               ? "str-json"
                               : options.key_kind == KeyKind::WikiTitles
                                     ? "wiki-titles"
                                     : "external_string_file";
    run_maps(key_label, data, options.duration_ms);
  } else if (options.key_kind == KeyKind::Uuid) {
    auto data = make_data<std::array<std::uint8_t, 16>>(
        static_cast<std::size_t>(entries), uuid_key);
    run_maps("uuid", data, options.duration_ms);
  } else {
    auto data = make_data<std::uint64_t>(static_cast<std::size_t>(entries), mix);
    run_maps("u64", data, options.duration_ms);
  }
}
