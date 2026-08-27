# CalderaDB

CalderaDB is an access-aware, tiered NoSQL document database written in C11. It combines low-latency in-memory operations with durable append-only disk storage by automatically promoting frequently accessed ("hot") data and demoting colder records based on configurable eviction policies.

---

## Overview

CalderaDB provides high throughput and low-latency key-value/document operations via a two-tier storage hierarchy:
- **Hot Tier (Memory):** An in-memory hash table using FNV-1a hashing and LRU eviction, protected by `pthread_rwlock` for thread-safe concurrent reads and exclusive writes.
- **Cold Tier (Disk):** An append-only log file (`data.flux`) with 64-bit checksum verification per record and automated index reconstruction for crash recovery.
- **Storage Engine:** An adaptive coordinator that manages promotions (cold-to-hot on cache hits) and demotions (hot-to-cold upon reaching memory capacity).

---

## Architecture

```
+-------------------------------------------------------------+
|                           Client                            |
+-------------------------------------------------------------+
                              |
                              | TCP (Text Wire Protocol)
                              v
+-------------------------------------------------------------+
|                         TCP Server                          |
|         (Thread-safe connection handling, select/I/O)       |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                       Storage Engine                        |
|            (Tier Coordinator, Promotion & Demotion)         |
+-------------------------------------------------------------+
            |                                     |
            v (Hot Hits / Writes)                 v (Cold Reads / Evictions)
+------------------------+           +------------------------+
|        Hot Tier        |           |       Cold Tier        |
|  (In-Memory Hash Table |           |  (Append-Only Log:     |
|   pthread_rwlock, LRU) |           |   data.flux + xxHash)  |
+------------------------+           +------------------------+
```

### Component Details
1. **TCP Server:** Non-blocking connection management handling concurrent client requests and dispatching them to the storage coordinator.
2. **Storage Engine:** Routes `GET`, `SET`, and `DEL` operations between tiers. When the hot tier reaches its capacity threshold, the engine evicts cold documents to disk.
3. **Hot Tier:** Memory-resident store utilizing an open-addressed/chained hash table with automatic 2x resizing at 75% load factor and millisecond-accurate access tracking.
4. **Cold Tier:** Append-only log storage (`data.flux`) alongside an in-memory index. Write operations append binary records with trailing checksums; deletes are marked via tombstone index updates.

---

## Features

- **Tiered Storage Architecture:** Automatic data migration between RAM and disk based on access frequency.
- **Text-Based Wire Protocol:** Simple, human-readable TCP command interface compatible with `nc`, `telnet`, and standard client libraries.
- **Concurrent Access:** Hot tier concurrency managed via read/write locks (`pthread_rwlock_t`).
- **Data Integrity:** xxHash64 checksum embedded in every persisted record to catch data corruption.
- **Crash Recovery:** Fast startup recovery scanning `data.flux` to rebuild the index table.
- **Cold Tier Compaction:** Garbage collection and reclamation of space from superseded or deleted records.
- **Dynamic Hash Table:** FNV-1a hash distribution with dynamic rehashing at 0.75 load factor.
- **Graceful Shutdown:** Handles `SIGINT` and `SIGTERM` to flush writes and cleanly release system resources.

---

## Storage Format

Persisted records in `data.flux` use a fixed binary layout with variable-length key and payload fields followed by an integrity checksum:

```
+-------------------+--------------------+----------------------+-----------------------+-------------------+
|  id_len (4 Bytes) |     id (N Bytes)   | payload_len (4 Bytes)|   payload (M Bytes)   | checksum (8 Bytes)|
|    (uint32_t)     |      (char[])      |      (uint32_t)      |      (uint8_t[])      |  (uint64_t xxHash)|
+-------------------+--------------------+----------------------+-----------------------+-------------------+
```

### Binary Record Layout
| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `id_len` | `uint32_t` | 4 Bytes | Length of the document identifier string |
| `id` | `char[]` | $N$ Bytes | Raw document ID bytes (not null-terminated on disk) |
| `payload_len` | `uint32_t` | 4 Bytes | Length of the payload data in bytes |
| `payload` | `uint8_t[]` | $M$ Bytes | Document payload / serialized data |
| `checksum` | `uint64_t` | 8 Bytes | 64-bit checksum calculated over the payload |

On startup, `cold_tier_recover()` performs a sequential pass over `data.flux`, validating each record's length and checksum before loading index entries into memory.

---

## Wire Protocol Reference

CalderaDB communicates over standard TCP using newline-delimited (`\n`) text commands. Responses follow a prefixed return format (`+` for success, `-` for errors, `$` for values/counts).

| Command | Syntax | Success Response | Error Response | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`PING`** | `PING` | `+PONG` | `-ERR <msg>` | Health check / ping server |
| **`SET`** | `SET <key> <value>` | `+OK` | `-ERR <msg>` | Insert or update document |
| **`GET`** | `GET <key>` | `+<value>` | `$-1` (not found) | Retrieve document value by key |
| **`DEL`** | `DEL <key>` | `+OK` | `-ERR NOT_FOUND` | Remove document from storage |
| **`STATS`** | `STATS` | `+<stats_string>` | `-ERR <msg>` | Retrieve server and tier metrics |

---

## Project Structure

```
CalderaDB/
├── include/calderadb/             # Public & internal headers
│   ├── calderadb.h                # Master include file
│   ├── core/types.h               # Core types (document_t, doc_id_t, etc.)
│   ├── engine/engine.h            # Storage engine & tier coordinator
│   ├── hot/hotTier.h              # Hot tier API & structures
│   ├── cold/coldTier.h            # Cold tier API & persistence
│   ├── eviction/evictionPolicy.h  # LRU/LFU/Sliding window eviction policies
│   ├── network/server.h           # Non-blocking TCP server
│   └── util/hashTable.h           # FNV-1a hash table implementation
├── src/                           # C source implementations
│   ├── coldTier.c                 # Append-only log and recovery logic
│   ├── config.c                   # Configuration loader and defaults
│   ├── hashTable.c                # Hash table with dynamic resizing
│   ├── hotTier.c                  # Concurrent memory store & LRU tracking
│   ├── main.c                     # Entrypoint & CLI parser
│   ├── server.c                   # TCP server accept loop & worker dispatch
│   └── types.c                    # Type constructors & destructors
├── tests/                         # Unit and integration test suites
│   ├── testColdTier.c             # Cold tier durability, recovery & tombstone tests
│   ├── testEngine.c               # Engine coordinator, cold fallback & rwlock tests
│   ├── testEviction.c             # O(1) LRU eviction order & capacity tests
│   ├── testHashTable.c            # Hash table unit tests
│   └── testHotTier.c              # Hot tier concurrency and LRU tests
├── bench/                         # Latency and throughput benchmarks
│   ├── benchLatency.c             # Operation latency benchmarks (p50/p95/p99)
│   └── benchThroughput.c          # Spillover & tiered throughput benchmarks
├── scripts/                       # Helper scripts (benchmarking, profiling)
│   └── runBenchmarks.sh
└── Makefile                       # GNU Make build definition
```

---

## Building

### Requirements
- GCC 4.9+ or Clang (supporting C11 standard)
- GNU Make
- POSIX-compliant operating system (Linux / macOS)
- Standard libraries: `pthread`, `m`

### Build Targets

```bash
# Build the production server binary (bin/calderadb)
make

# Build and run the entire test suite (5 suites)
make test

# Build and execute performance benchmarks
make bench

# Build and test with AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer
make debug

# Run memory leak and correctness checks under Valgrind
make valgrind

# Remove compiled objects and binaries
make clean
```

---

## Configuration

CalderaDB can be configured via CLI flags or default environment settings.

### Command-Line Arguments
| Option | Default Value | Description |
| :--- | :--- | :--- |
| `--port <port>` | `9090` | TCP port for incoming client connections |
| `--data-dir <path>` | `/tmp/calderadb` | Filesystem directory for cold tier storage files |
| `--hot-capacity <MB>` | `512` | Maximum RAM capacity allocated for hot tier (in MB) |

### Starting the Server

```bash
# Run with default settings (port 9090, 512MB RAM, /tmp/calderadb data dir)
./bin/calderadb

# Run with custom parameters
./bin/calderadb --port 6380 --data-dir ./data --hot-capacity 1024
```

---

## Performance & Benchmarks

Measured on modern Linux x86_64 hardware with release flags (`-O2`):

### 1. Operation Latency (`bin/benchLatency` - 10,000 documents in Hot Tier)

| Operation | Throughput | p50 Latency | p95 Latency | p99 Latency | Max Latency |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Sequential Insert** | ~3,880,000 ops/sec | — | — | — | — |
| **Sequential Read** | ~8,040,000 ops/sec | 0.080 μs | 0.264 μs | 0.532 μs | 7.369 μs |
| **Random Read** | ~3,370,000 ops/sec | 0.217 μs | 0.692 μs | 1.144 μs | 14.998 μs |

### 2. Tier Spillover Throughput (`bin/benchThroughput` - 650,000 documents, 128MB RAM boundary)

| Metric | Measured Value | Description |
| :--- | :--- | :--- |
| **Bulk Insert Throughput** | ~2,401,000 ops/sec | 650,000 documents inserted in 270 ms |
| **Random Read Throughput** | ~1,362,000 ops/sec | 650,000 reads executed across hot + cold tiers |
| **Hot Tier Hit Rate** | **90.60%** (588,930 reads) | Satisfied directly in RAM at sub-microsecond latency |
| **Cold Tier Hit Rate** | **9.40%** (61,070 reads) | Retrieved from append-only disk log |

---

## Testing & Quality Assurance

CalderaDB includes 5 dedicated unit test suites:
- **`testColdTier`**: Append verification, real xxHash64 checksum validation, crash recovery index rebuilding, and disk tombstone persistence across restarts.
- **`testEngine`**: Top-level coordinator CRUD, automatic promotion, cold-tier fallback for oversize documents, and multi-threaded reader concurrency.
- **`testEviction`**: $\mathcal{O}(1)$ LRU doubly-linked list eviction order and memory threshold compliance.
- **`testHashTable`**: Dynamic resizing at 75% load factor, collision chaining, and key removal.
- **`testHotTier`**: Concurrency lock verification and boundary checks.

```bash
# Execute unit tests
make test

# Run tests under AddressSanitizer + LeakSanitizer + UBSan
make debug
```

---

## Roadmap

### ✅ Implemented

| Area | Detail |
| :--- | :--- |
| **Two-tier storage** | Hot (RAM hash table) + Cold (append-only binary log) |
| **Automatic promotion** | Cold $\to$ Hot on cache-miss read |
| **$\mathcal{O}(1)$ LRU eviction** | Intrusive doubly-linked list with head/tail pointers |
| **Cold-tier fallback** | `engine_set` automatically falls back to disk when payload exceeds hot capacity |
| **Crash recovery** | Sequential log scan rebuilds offset index at startup |
| **Persisted tombstones** | Deleted keys write `0xFFFFFFFF` tombstones to prevent resurrection on restart |
| **Cold-tier compaction** | Single-pass atomic rewrite via temp file + `rename` |
| **Checksum integrity** | Real canonical xxHash64 per record, verified on cold read |
| **Concurrent access** | `pthread_rwlock_t` on hot tier and engine layer for parallel readers; atomic metrics |
| **TCP wire protocol** | Newline-delimited text; `PING / GET / SET / DEL / STATS` |
| **Persistent connections** | Keep-alive loop with thread-per-client dispatch |
| **Graceful shutdown** | `SIGINT`/`SIGTERM` flush and clean resource release |
| **Build system** | GNU Make with `test`, `bench`, `debug` (ASan/UBSan/LSan), `valgrind` targets |
| **Unit test suite** | 5 test suites covering cold tier, engine, eviction, hashtable, hot tier |
| **Benchmarks** | `benchLatency` (p50/p95/p99) and `benchThroughput` (spillover hit ratios) |

---

### 🔧 Planned (v1.1 scope)

#### Protocol & Feature Additions
- [ ] `KEYS <prefix>` prefix scanning command
- [ ] `TTL` / `EXPIRE` background expiration policy
- [ ] Explicit `COMPACT` administrative TCP command
- [ ] Configuration file loader (`calderadb.conf`)

#### Advanced Storage & Indexing
- [ ] Sliding-window access frequency tracking
- [ ] Configurable eviction policies (LFU / adaptive ARC)
- [ ] Epoll-based event loop for 10k+ concurrent client connections

---

## License

MIT License. See [LICENSE](LICENSE) for details.