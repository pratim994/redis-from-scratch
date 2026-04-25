# Redis


A production-quality, Redis-like in-memory key-value store written in modern C++17.  

---
## High level system design 

<img width="1736" height="578" alt="Screenshot from 2026-04-25 10-58-59" src="https://github.com/user-attachments/assets/e1843429-80e4-4791-b4f6-f5eba2fa40dc" />

---

## Architecture

```
src/
  network/    server.cpp      – TCP accept loop (poll-based), connection lifecycle
              client.cpp      – CLI client
  core/       commands.cpp    – Command parsing and dispatch (GET/SET/DEL/ZADD/…)
  storage/    avl.cpp         – Self-balancing AVL tree (irank queries via cnt field)
              hashtable.cpp   – Progressive-rehashing hash map (two-table design)
              zset.cpp        – Sorted set: AVL tree + hash map, Redis ZSET semantics
  concurrency/thread_pool.cpp – std::thread pool with graceful shutdown
  utils/      heap.cpp        – Min-heap for TTL timer management
include/                      – All headers
tests/                        – GoogleTest unit tests
```

### Key design decisions

| Decision | Rationale |
|---|---|
| Intrusive data structures | Zero heap allocations per node lookup; better cache locality |
| Progressive rehashing | Hash map never pauses; migration spread across O(1) operations |
| AVL `cnt` field | O(log n) rank and offset queries (needed by `zquery`) |
| Buffer read-offset | O(1) consume from front; no O(n) `erase` from vector beginning |
| Thread pool for large deletes | Avoid blocking the event loop when freeing large ZSets |
| `std::function` task queue | Type-safe, captures lambdas naturally, no raw function-pointer casts |
| `string_view` in hot paths | Avoids copies in lookup/comparison code |

---

## Features

- **String commands**: `GET`, `SET`, `DEL`
- **TTL support**: `PEXPIRE`, `PTTL` (millisecond precision, min-heap eviction)
- **Sorted set**: `ZADD`, `ZREM`, `ZSCORE`, `ZQUERY` (range query with offset/limit)
- **Key listing**: `KEYS`
- **Pipelining**: Multiple requests per TCP read handled in one loop iteration
- **Idle connection timeout**: 5-second idle connections are reaped automatically
- **Async large-object deletion**: ZSets > 1000 members are freed on the thread pool

---

## Build

### Prerequisites

- CMake ≥ 3.16
- GCC ≥ 9 or Clang ≥ 10 (C++17 required)
- Internet access (CMake fetches GoogleTest automatically)

### Build all targets

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run tests

```bash
cd build
ctest --output-on-failure
# or run directly:
./tests
```

---

## Usage

Start the server (default port 1234):

```bash
./build/server
```

In another terminal, use the client:

```bash
# String commands
./build/client set mykey "hello world"
./build/client get mykey

# TTL
./build/client set session_id abc123
./build/client pexpire session_id 5000   # expire in 5 seconds
./build/client pttl session_id

# Delete
./build/client del mykey

# Sorted set
./build/client zadd leaderboard 1500 alice
./build/client zadd leaderboard 2300 bob
./build/client zadd leaderboard 1900 carol
./build/client zscore leaderboard bob
./build/client zquery leaderboard 0 "" 0 10   # all members, offset 0, limit 10
./build/client zrem leaderboard alice

# List all keys
./build/client keys
```

---

## Wire Protocol

The protocol is a simple length-prefixed binary format:

```
Request:  [4-byte total len][4-byte nargs][4-byte arglen][argbytes]...
Response: [4-byte body len][1-byte tag][payload...]
```

Tags: `0`=nil, `1`=error, `2`=string, `3`=int64, `4`=double, `5`=array.

---

## Performance Notes

- The event loop is single-threaded (`poll`-based); heavy work is offloaded via the thread pool.
- Hash map rehashing is progressive: at most 128 nodes migrated per operation.
- Buffer consume is O(1) (read offset), compacted lazily when half the buffer is dead.
- AVL tree operations are O(log n); no global locks are held during tree traversal.
- For higher throughput on Linux, replace `poll` with `epoll` (the `handle_read`/`handle_write` callbacks are already decoupled from the polling mechanism).

oduction-quality, Redis-like in-memory key-value store written in modern C++17.  

---

## Architecture

```
src/
  network/    server.cpp      – TCP accept loop (poll-based), connection lifecycle
              client.cpp      – CLI client
  core/       commands.cpp    – Command parsing and dispatch (GET/SET/DEL/ZADD/…)
  storage/    avl.cpp         – Self-balancing AVL tree (irank queries via cnt field)
              hashtable.cpp   – Progressive-rehashing hash map (two-table design)
              zset.cpp        – Sorted set: AVL tree + hash map, Redis ZSET semantics
  concurrency/thread_pool.cpp – std::thread pool with graceful shutdown
  utils/      heap.cpp        – Min-heap for TTL timer management
include/                      – All headers
tests/                        – GoogleTest unit tests
```

### Key design decisions

| Decision | Rationale |
|---|---|
| Intrusive data structures | Zero heap allocations per node lookup; better cache locality |
| Progressive rehashing | Hash map never pauses; migration spread across O(1) operations |
| AVL `cnt` field | O(log n) rank and offset queries (needed by `zquery`) |
| Buffer read-offset | O(1) consume from front; no O(n) `erase` from vector beginning |
| Thread pool for large deletes | Avoid blocking the event loop when freeing large ZSets |
| `std::function` task queue | Type-safe, captures lambdas naturally, no raw function-pointer casts |
| `string_view` in hot paths | Avoids copies in lookup/comparison code |

---

## Features

- **String commands**: `GET`, `SET`, `DEL`
- **TTL support**: `PEXPIRE`, `PTTL` (millisecond precision, min-heap eviction)
- **Sorted set**: `ZADD`, `ZREM`, `ZSCORE`, `ZQUERY` (range query with offset/limit)
- **Key listing**: `KEYS`
- **Pipelining**: Multiple requests per TCP read handled in one loop iteration
- **Idle connection timeout**: 5-second idle connections are reaped automatically
- **Async large-object deletion**: ZSets > 1000 members are freed on the thread pool

---

## Build

### Prerequisites

- CMake ≥ 3.16
- GCC ≥ 9 or Clang ≥ 10 (C++17 required)
- `pthread`
- Internet access (CMake fetches GoogleTest automatically)

### Build all targets

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run tests

```bash
cd build
ctest --output-on-failure
# or run directly:
./tests
```

---

## Usage

Start the server (default port 1234):

```bash
./build/server
```

In another terminal, use the client:

```bash
# String commands
./build/client set mykey "hello world"
./build/client get mykey

# TTL
./build/client set session_id abc123
./build/client pexpire session_id 5000   # expire in 5 seconds
./build/client pttl session_id

# Delete
./build/client del mykey

# Sorted set
./build/client zadd leaderboard 1500 alice
./build/client zadd leaderboard 2300 bob
./build/client zadd leaderboard 1900 carol
./build/client zscore leaderboard bob
./build/client zquery leaderboard 0 "" 0 10   # all members, offset 0, limit 10
./build/client zrem leaderboard alice

# List all keys
./build/client keys
```

---

## Wire Protocol

The protocol is a simple length-prefixed binary format:

```
Request:  [4-byte total len][4-byte nargs][4-byte arglen][argbytes]...
Response: [4-byte body len][1-byte tag][payload...]
```

Tags: `0`=nil, `1`=error, `2`=string, `3`=int64, `4`=double, `5`=array.

---

## Performance Notes

- The event loop is single-threaded (`poll`-based); heavy work is offloaded via the thread pool.
- Hash map rehashing is progressive: at most 128 nodes migrated per operation.
- Buffer consume is O(1) (read offset), compacted lazily when half the buffer is dead.
- AVL tree operations are O(log n); no global locks are held during tree traversal.


---
## Results 
<img width="774" height="858" alt="Screenshot from 2026-04-25 11-44-46" src="https://github.com/user-attachments/assets/2b8e6644-f6fc-4279-9463-14afb235a323" /><img width="735" height="944" alt="Screenshot from 2026-04-25 11-45-12" src="https://github.com/user-attachments/assets/e49071a6-cfac-4328-8ec4-28c45caee326" />


