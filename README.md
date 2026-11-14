# bbtree-cpp

A small, single-threaded, in-memory B-tree ordered map for C++17. It supports insert or replace, lookup, delete, and sorted traversal. The header has no external dependencies. Nodes hold up to 31 key-value pairs by default; `MinDegree` is a template parameter.

This is an independent C++ implementation, not a port of [Xiangpeng Hao's `bbtree`](https://github.com/XiangpengHao/bbtree). The Rust project has SIMD search, batch APIs, and a concurrent map. This library has none of those features.

```cpp
#include "bbtree/btree.hpp"

bbtree::BTreeMap<int, int> tree;
tree.insert_or_assign(7, 42);
const int* value = tree.get(7);  // nullptr if absent
*tree.get_mut(7) = 43;
tree.erase(7);
tree.for_each([](const int& key, const int& value) {
    // Visits entries in key order.
});
tree.for_each_from(3, 100, [](const int& key, const int& value) {
    // Visits at most 100 entries with key >= 3.
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

This reruns the scalar workloads from the [blog's single-thread card](https://bbtree.xiangpeng.systems/post/) using its [evaluation harness](https://github.com/XiangpengHao/bbtree-eval/blob/main/blog/runs.sh) as the protocol. Each map gets 10 million entries. Inputs are prepared before timing. Mixed lookups sample a balanced hit/miss pool; updates change existing values; scans read up to 100 entries from a present key. Those three workloads run for one second each. Insert times one pass of 10 million new keys into an empty map. Each cell below is one run, in nanoseconds per operation; lower is better.

Measured November 14, 2026, on an Apple M1 Pro MacBook Pro with 16 GB RAM. C++ used AppleClang 21.0.0, C++17, and CMake Release (`-O3 -DNDEBUG`). Rust used `rustc 1.95.0`, the evaluation harness's fat-LTO release profile and mimalloc. The C++ maps used the system allocator. The Rust `bbtree` column is the shared `BBTreeMap` used in the blog, called from one timed thread. Rust and C++ runs were serial. The generated `u64` and UUID keys follow the harness's SplitMix64 stream; JSON-shaped string keys came from its `--dump-keys` export.

| Keys | Operation | C++ B-tree | Rust `bbtree` | C++ `std::map` | Rust `BTreeMap` |
| --- | --- | ---: | ---: | ---: | ---: |
| `u64` | Get, about half hits | 446.70 | 250.40 | 1143.48 | 361.62 |
| `u64` | Insert | 495.27 | 292.75 | 1159.25 | 377.01 |
| `u64` | Update | 371.16 | 226.98 | 1212.57 | 372.44 |
| `u64` | Scan 100 | 1417.30 | 736.80 | 10163.46 | 1452.91 |
| UUID | Get, about half hits | 666.39 | 259.60 | 1074.07 | 431.55 |
| UUID | Insert | 730.13 | 293.44 | 1118.35 | 408.44 |
| UUID | Update | 702.00 | 235.77 | 1106.51 | 444.65 |
| UUID | Scan 100 | 1798.48 | 889.59 | 9373.37 | 2149.10 |
| JSON-shaped strings | Get, about half hits | 948.33 | 295.54 | 1003.19 | 986.47 |
| JSON-shaped strings | Insert | 827.61 | 392.55 | 1097.28 | 894.17 |
| JSON-shaped strings | Update | 950.47 | 275.51 | 1121.03 | 1013.44 |
| JSON-shaped strings | Scan 100 | 1930.15 | 1138.74 | 9680.01 | 2775.69 |

The blog also times native Rust batch APIs. On this machine, Rust `bbtree` measured 91.55, 99.37, and 139.59 ns per key for batches of 64 lookups with `u64`, UUID, and JSON-shaped keys respectively; batches of 16 inserts measured 149.16, 169.73, and 229.92 ns per key. This C++ map has no equivalent batch API, so the C++ benchmark reports those rows as unsupported.

The C++ benchmark runs with:

```sh
./build/bbtree_bench 10000000 1000 --keys u64
./build/bbtree_bench 10000000 1000 --keys uuid
./build/bbtree_bench 10000000 1000 --keys str-json --keys-file /path/to/str-json.keys
```

The key file is the text format produced by `bbtree-scaling --dump-keys`: a `present absent` count header, then one key per line, present keys first. `--keys wiki-titles --keys-file /path/to/wiki.keys` uses the same format. The original Wikipedia dataset was unavailable from the public benchmark-data release when these runs were made, so that tab is unmeasured.

The Rust measurements used [evaluation commit `01229ec`](https://github.com/XiangpengHao/bbtree-eval/commit/01229ec0c07c30a89ae379de3179f4ce5b3b9361) with [library commit `23aeb9d`](https://github.com/XiangpengHao/bbtree/commit/23aeb9dca642efa829a96736e065157307bdbe72). A temporary adapter outside this repository unwrapped the newer library's `Result`-returning insert calls; it did not change the workload loops. The clean library revision recorded with the blog's original machine logs does not build with the public evaluation harness's available revisions. Rust's shared map is populated on all available CPUs before the single-thread read workloads, while the C++ map is populated serially. These are local reruns of the blog's scalar workloads, not a reproduction of its original Ryzen 9 9900X results or its concurrent, memory, and end-to-end cards.

## License

MIT. See [LICENSE](LICENSE).
