# Redis-like Key Value Store in C++

A simple Redis-inspired in-memory key-value server built in C++.

The project focuses on understanding how a basic database server works internally, including networking, request handling, serialization, and hash tables.

## Features

- TCP client-server communication
- Non-blocking sockets
- `poll()` based event loop
- Multiple client connections
- Custom binary request/response protocol
- Custom serialization and deserialization
- In-memory key-value storage
- `GET`, `SET`, and `ERASE` commands
- Custom hash table with separate chaining
- Dynamic hash table resizing
- Incremental rehashing
- Benchmarking with multiple concurrent clients

## Architecture

```text
Client
   |
   | TCP
   ↓
Server
   |
   ↓
poll() Event Loop
   |
   ├── Read Request
   |
   ├── Deserialize
   |
   ├── Process Command
   |       |
   |       ↓
   |   Hash Table
   |
   ├── Create Response
   |
   └── Write Response