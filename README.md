# CalderaDB

CalderaDB is an access-aware, tiered NoSQL document database written in C11. It combines low-latency in-memory operations with durable append-only disk storage by automatically promoting frequently accessed "hot" data and demoting colder records.

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
│   ├── testColdTier.c             # Cold tier durability and recovery tests
│   ├── testEviction.c             # Eviction policy evaluation tests
│   ├── testHashTable.c            # Hash table unit tests
│   └── testHotTier.c              # Hot tier concurrency and LRU tests
├── bench/                         # Latency and throughput benchmarks
│   ├── benchLatency.c             # Operation latency benchmarks
│   └── benchThroughput.c          # Multi-threaded throughput benchmarks
├── scripts/                       # Helper scripts (benchmarking, profiling)
│   └── runBenchmarks.sh
└── MakeFile                       # GNU Make build definition
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

# Build and run the test suite
make test

# Build and execute performance benchmarks
make bench

# Build with AddressSanitizer and UndefinedBehaviorSanitizer
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

## Usage

You can connect and interact with CalderaDB using any standard TCP utility (such as `nc`, `telnet`, or `socat`).

### Example Session with `nc`

```bash
# Store a document
$ echo 'SET user:1001 {"name":"Alice","role":"admin"}' | nc localhost 9090
+OK

# Retrieve a document
$ echo 'GET user:1001' | nc localhost 9090
+{"name":"Alice","role":"admin"}

# Inspect database metrics
$ echo 'STATS' | nc localhost 9090
+gets:1 sets:1 dels:0 hot_bytes:48 hot_docs:1 cold_bytes:0 cold_docs:0

# Delete a document
$ echo 'DEL user:1001' | nc localhost 9090
+OK

# Verify deletion
$ echo 'GET user:1001' | nc localhost 9090
$-1
```

---

## Testing & Quality Assurance

CalderaDB includes a comprehensive unit test suite covering tier operations, resizing, concurrent access, and file recovery.

```bash
# Execute unit tests
make test

# Run tests under Valgrind for memory leak checks
valgrind --leak-check=full --show-leak-kinds=all ./bin/calderadb_tests
```

---

## License

MIT License. See [LICENSE](file:///home/ciaran/Projects/c/CalderaDB/LICENSE) for details.