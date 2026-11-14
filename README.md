# bbtree-cpp

A small, single-threaded, in-memory B-tree ordered map for C++17. It supports insert or replace, lookup, delete, and sorted traversal. The header has no external dependencies. Nodes hold up to 31 key-value pairs by default; `MinDegree` is a template parameter.

This is an independent C++ implementation, not a port of [Xiangpeng Hao's `bbtree`](https://github.com/XiangpengHao/bbtree). The Rust project has SIMD search, batch APIs, and a concurrent map. This library has none of those features.

```cpp
#include "bbtree/btree.hpp"

bbtree::BTreeMap<int, int> tree;
tree.insert_or_assign(7, 42);
const int* value = tree.get(7);  // nullptr if absent
tree.erase(7);
tree.for_each([](const int& key, const int& value) {
    // Visits entries in key order.
});
```

`insert_or_assign` returns `true` for a new key and `false` for a replacement. `erase` returns whether a key existed. A mutation invalidates pointers returned by `get`. The map is not thread-safe. Keys and values must be copyable for deletion, and must be move assignable for insertion and rebalancing.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Performance

Measured on November 14, 2026, on an Apple M1 Pro MacBook Pro with 16 GB RAM. C++ used AppleClang 21.0.0 with CMake Release flags (`-O3 -DNDEBUG`). Rust used `rustc 1.95.0` and Cargo's release profile. The Rust benchmark pins [upstream commit `23aeb9d`](https://github.com/XiangpengHao/bbtree/commit/23aeb9dca642efa829a96736e065157307bdbe72) and uses its single-threaded `unsync::BBTreeMap`.

Each run inserts 10 million distinct `u64` keys in deterministic shuffled order into a fresh map, then performs 10 million lookups with half hits and half misses. Keys use `(uint64_t(uint32_t(i * 0x9e3779b1)) << 1) | 1`; lookup positions use `uint32_t(i * 0x85ebca6b) % 10000000`, and odd lookup iterations query `key - 1` for a miss. The table shows the median of three repetitions, in nanoseconds per operation. Tree construction before inserts and destruction after lookups are excluded. The C++ and Rust binaries ran separately. All four maps returned the same lookup checksum of `74955957732672`.

| Map | Insert, ns/op | Lookup, ns/op |
| --- | ---: | ---: |
| This C++ B-tree | 243.4 | 345.0 |
| C++ `std::map` | 71.8 | 1046.9 |
| Rust `bbtree::unsync::BBTreeMap` | 215.8 | 195.0 |
| Rust `std::collections::BTreeMap` | 164.6 | 361.5 |

Under this workload, the C++ tree's lookup is 1.77 times slower than Rust `bbtree` and 3.03 times faster than C++ `std::map`. Its insertion is 1.13 times slower than Rust `bbtree`. The [original blog post](https://bbtree.xiangpeng.systems/post/) reports up to 5 times faster single-threaded performance for Rust `bbtree` against Rust's standard `BTreeMap` on its own hardware and workload. These local numbers measure a different workload and machine; they are not a reproduction of that headline figure. No concurrent, batch, memory-use, or tail-latency comparison was performed.

Run the C++ benchmark with:

```sh
./build/bbtree_bench 10000000 3
```

The Rust figures came from a separate driver using the same key and lookup loops against the pinned upstream revision. The driver is outside this C++ repository. CPU load and thermal state can move the numbers, so rerun on your machine for local decisions.

## License

MIT. See [LICENSE](LICENSE).
