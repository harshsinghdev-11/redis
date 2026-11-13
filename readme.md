# Redis-like In-Memory Key-Value Store in C++

A lightweight, high-performance Redis clone built from first principles in C++17. It features a non-blocking `poll()` event loop, a custom binary request/response wire protocol with typed serialization, and an in-memory hash table with incremental rehashing.

---

## Table of Contents
1. [Architecture](#architecture)
2. [How to Start the Server](#how-to-start-the-server)
3. [How to Connect to the Server](#how-to-connect-to-the-server)
4. [Wire Protocol Specification](#wire-protocol-specification)
5. [Request Structure](#request-structure)
6. [Response Structure & Serialization Tags](#response-structure--serialization-tags)
7. [Supported Commands & Error Codes](#supported-commands--error-codes)
8. [Benchmarking](#benchmarking)
9. [Prerequisites & Build](#prerequisites--build)

---

## Architecture

```text
Client (clients/client_serialisation.cpp)
   │
   │  TCP Socket (127.0.0.1:1234)
   ▼
Server (src/main.cpp)
   │
   ▼
poll() Event Loop (Non-blocking I/O)
   │
   ├── 1. Read Request into incoming buffer (handle_read)
   │
   ├── 2. Frame Extraction & Deserialization (try_one_request, parse_req)
   │
   ├── 3. Execute Command (do_request)
   │       │
   │       ▼
   │   Custom Hashtable (Separate Chaining + Incremental Rehashing)
   │
   ├── 4. Typed Serialization (src/serialisation.cpp: out_nil, out_str, out_err, etc.)
   │
   └── 5. Write Response to outgoing buffer & flush to client (handle_write)
```

---

## How to Start the Server

1. Open your terminal (or WSL terminal).
2. Navigate to the project root directory:
   ```bash
   cd /path/to/redis
   ```
3. If you have not compiled the server yet, build it using:
   ```bash
   g++ -std=c++17 -Wall -Wextra -Iinclude src/*.cpp -o redis_server
   ```
   *(For full build options and environment details, see [Prerequisites & Build](#prerequisites--build)).*
4. Run the server executable:
   ```bash
   ./redis_server
   ```
5. The server will bind to `0.0.0.0:1234`, set the socket to non-blocking mode, and start the `poll()` event loop:
   ```text
   Connection fd is: 3
   socket is configured
   event loop is started
   Calling poll...
   ```

---

## How to Connect to the Server

### 1. Connection Details
- **Protocol:** TCP (raw stream)
- **Host / IP:** `127.0.0.1` (loopback)
- **Port:** `1234`
- **Handshake:** None required. Once connected, clients can immediately send formatted request frames.

### 2. Using the Built-In Client
In a separate terminal, compile and run the reference serialization client:
```bash
g++ -std=c++17 -Wall -Wextra -Iinclude clients/client_serialisation.cpp -o client_serialisation
./client_serialisation
```
The client connects, issues a `SET name Harsh` command, followed by `GET name`, and outputs the typed parsed responses:
```text
string length: 3
string added
string length: 4
string added
string length: 5
string added
total size: 24
Send data: 28
(nil)
(str) "Harsh"
```

### 3. Programmatic Connection Workflow (C++ Example)
```cpp
// 1. Create TCP Stream Socket
int fd = socket(AF_INET, SOCK_STREAM, 0);

// 2. Connect to 127.0.0.1:1234
struct sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_port = htons(1234);
addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

// 3. Send framed request bytes via send()
send(fd, request_bytes.data(), request_bytes.size(), 0);

// 4. Read 4-byte response header, then read body bytes via read()/recv()
```

---

## Wire Protocol Specification

| Property | Value | Description |
| :--- | :--- | :--- |
| **Transport** | TCP | Byte stream; all messages are strictly length-prefixed. |
| **Default Port** | `1234` | Configurable in `main.cpp` and client configs. |
| **Byte Order** | **Little-endian** | All `u32`, `i64`, `f64` fields are transmitted least-significant byte first. |
| **Max Message Size** | **1 MiB** (`1024 * 1024` bytes) | Guard against denial-of-service / buffer overflow attacks. |
| **Pipelining** | Supported | Clients may send multiple requests back-to-back without waiting for each response. |
| **Ordering** | FIFO | Responses are returned in the exact order of received requests. |

### Message Framing
Every message in both directions begins with a 4-byte frame length:

```text
+------------------------+---------------------------------------+
|  u32 body_len (4 B)    |        body (body_len bytes)          |
+------------------------+---------------------------------------+
```
- The receiver reads the 4-byte header to determine `body_len`.
- It then buffers incoming bytes until exactly `body_len` bytes have arrived.
- If `body_len > 1 MiB`, the server aborts and closes the connection.

---

## Request Structure

A request body is an array of strings. The first string is the **command name** (e.g., `set`, `get`, `erase`), and subsequent strings are its **arguments**.

### Binary Layout
```text
+-------------------+-------------------+-------------------+-------------------+-----+
|   u32 body_len    |     u32 nstr      |    u32 len_1      |   bytes_1 (data)  | ... |
|     (4 bytes)     |     (4 bytes)     |     (4 bytes)     |   (len_1 bytes)   | ... |
+-------------------+-------------------+-------------------+-------------------+-----+
```

| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `body_len` | `uint32_t` | 4 bytes | Total length of the request body following this header. |
| `nstr` | `uint32_t` | 4 bytes | Number of string arguments in this command. |
| `len_N` | `uint32_t` | 4 bytes | Byte length of string $N$. |
| `bytes_N` | `uint8_t[]` | `len_N` bytes | Raw string data (binary-safe, not null-terminated). |

### Wire Example: `SET name Harsh`
* Command array: `["set", "name", "Harsh"]` (`nstr = 3`)
* String 1: `"set"` (length = 3)
* String 2: `"name"` (length = 4)
* String 3: `"Harsh"` (length = 5)
* Body length: `4 (nstr) + (4 + 3) + (4 + 4) + (4 + 5) = 28` bytes (`0x1C`).

```text
Hex Dump:
1C 00 00 00             # body_len = 28 bytes
03 00 00 00             # nstr = 3
03 00 00 00  73 65 74   # len = 3, "set"
04 00 00 00  6E 61 6D 65# len = 4, "name"
05 00 00 00  48 61 72 73 68 # len = 5, "Harsh"
```

---

## Response Structure & Serialization Tags

Responses use a **typed tag-based encoding** defined in `include/serialisation.h` and implemented in `src/serialisation.cpp`.

### Response Framing
```text
+------------------------+-------------------+-----------------------------------+
|  u32 body_len (4 B)    |   u8 tag (1 B)    |       payload (variable)          |
+------------------------+-------------------+-----------------------------------+
```

### Type Tags (`enum : uint8_t`)

| Tag Constant | Value (Hex) | Type | Wire Layout After Tag Byte |
| :--- | :---: | :--- | :--- |
| `TAG_NIL` | `0x00` | Nil / Void | *None* (0 bytes) |
| `TAG_ERR` | `0x01` | Error | `[u32 code][TAG_STR (0x02)][u32 len][string bytes]` |
| `TAG_STR` | `0x02` | String | `[u32 len][len bytes]` |
| `TAG_INT` | `0x03` | 64-bit Signed Integer | `[i64 value]` (8 bytes, little-endian) |
| `TAG_DBL` | `0x04` | 64-bit Float (Double) | `[f64 value]` (8 bytes, IEEE 754 little-endian) |
| `TAG_ARR` | `0x05` | Array of Values | `[u32 count][element_1][element_2]...` |

---

### Detailed Response Layouts & Examples

#### 1. `TAG_NIL` (Null / Success without return value)
Returned by `set` and `erase`.
```text
01 00 00 00              # body_len = 1
00                       # TAG_NIL
```

#### 2. `TAG_STR` (String)
Returned by `get <key>` on success.
```text
0A 00 00 00              # body_len = 10
02                       # TAG_STR
05 00 00 00              # string length = 5
48 61 72 73 68           # "Harsh"
```

#### 3. `TAG_ERR` (Error with Nested String)
Errors nest a complete `TAG_STR` value for the human-readable message.
```text
1B 00 00 00              # body_len = 27
01                       # TAG_ERR
94 01 00 00              # code = 404 (0x00000194, little-endian)
02                       # TAG_STR
11 00 00 00              # string length = 17
4B 65 79 20 44 6F 65 73  # "Key Doesn't exist"
6E 27 74 20 65 78 69 73 74
```

#### 4. `TAG_INT` (64-bit Integer)
```text
09 00 00 00              # body_len = 9
03                       # TAG_INT
2A 00 00 00 00 00 00 00  # value = 42 (int64, little-endian)
```

#### 5. `TAG_DBL` (64-bit Double)
```text
09 00 00 00              # body_len = 9
04                       # TAG_DBL
00 00 00 00 00 00 F0 3F  # value = 1.0 (IEEE 754 double, little-endian)
```

#### 6. `TAG_ARR` (Array)
Arrays are recursive; each element is preceded by its own tag.
```text
17 00 00 00              # body_len = 23
05                       # TAG_ARR
02 00 00 00              # count = 2 elements
03 01 00 00 00 00 00 00 00 # element 1: TAG_INT with value = 1
03 02 00 00 00 00 00 00 00 # element 2: TAG_INT with value = 2
```

---

## Supported Commands & Error Codes

| Command | Arguments | Success Response | Failure Response |
| :--- | :--- | :--- | :--- |
| `get` | `<key>` | `TAG_STR <value>` | `TAG_ERR 404 "Key Doesn't exist"` |
| `set` | `<key> <value>` | `TAG_NIL` | — |
| `erase` | `<key>` | `TAG_NIL` | `TAG_ERR 404 "Key Doesn't exist"` |
| `subscribe` | `<ch1> [ch2 ...]` | `TAG_ARR(3) ["subscribe", ch, count]` | `TAG_ERR 400 "ERR wrong number of arguments"` |
| `unsubscribe` | `[ch1 ...]` | `TAG_ARR(3) ["unsubscribe", ch, remaining]` | — |
| `publish` | `<channel> <message>` | `TAG_INT <receivers_count>` | `TAG_ERR 400 "ERR wrong number of arguments"` |
| `ping` | `[<message>]` | `TAG_STR "PONG"` or `TAG_ARR(2) ["pong", msg]` | — |
| `quit` | — | `TAG_NIL` | — |
| *Unknown* | *any* | — | `TAG_ERR 404 "unrecognized command"` |
| *Context Error* | *Disallowed in Pub/Sub* | — | `TAG_ERR 400 "ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT allowed in this context"` |

> **Note:** Command names are lowercase and case-sensitive.

---

## Benchmarking

Performance tests measuring throughput and latency percentiles (P50, P95, P99) under concurrent load:

```bash
# Benchmark V2: Multi-client asynchronous pipelining
# Syntax: ./benchmarkV2 <num_clients> <requests_per_client> <COMMAND>
./benchmarkV2 10 1000 SET
./benchmarkV2 10 1000 GET
./benchmarkV2 100 1000 SET
```

* [Single Client Benchmark Results](benchmark/firstBenchmark.md)
* [Multi-Client Benchmark Results](benchmark/2_benchmark.md)
* [High-Concurrency Load Results](benchmark/3_benchmark.md)

---

## Prerequisites & Build

### Environment
The networking layer uses POSIX socket APIs (`<poll.h>`, `<sys/socket.h>`, `<unistd.h>`). On Windows, run inside **WSL (Windows Subsystem for Linux)**, MSYS2, or a Linux environment.

### Compiler Requirements
- `g++` or `clang++` with C++17 support

### Compilation Commands

```bash
# 1. Compile the Redis Server
g++ -std=c++17 -Wall -Wextra -Iinclude src/*.cpp -o redis_server

# 2. Compile the Serialization Client
g++ -std=c++17 -Wall -Wextra -Iinclude clients/client_serialisation.cpp -o client_serialisation

# 3. (Optional) Compile the Benchmark Tools
g++ -std=c++17 -O3 benchmark/benchmarkV2.cpp -o benchmarkV2
```
