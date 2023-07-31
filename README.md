# Persistent Key-Value Store

An embedded Linux database with a sorted in-memory index, checksummed write-ahead log and atomic compaction.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/kv_cli data set high_score 9000
./build/kv_cli data get high_score
./build/kv_cli data delete high_score
./build/kv_cli data compact
```
