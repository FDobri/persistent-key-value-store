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

Requires Linux, CMake 3.24+, a C++20 toolchain and Python 3 for the crash test. Run on the Linux filesystem when using WSL.

## Design

The store holds an exclusive process lock and expects one calling thread per handle. Each update appends a length-prefixed, CRC32-protected record and calls `fdatasync` before updating the index. Recovery replays complete frames, truncates an incomplete final frame and checks final-frame CRC before replay. Invalid headers and checksum failures before the final record surface as errors. Compaction writes a new log with the live values, syncs it before rename and syncs the containing directory before returning.

The Python test kills a writer at every byte boundary of a WAL append. Each reopen verifies a committed key, the interrupted key and a subsequent write. The test exercises process interruption, including the fully persisted record before acknowledgment. Storage power-loss behavior also depends on filesystem and device sync semantics.
