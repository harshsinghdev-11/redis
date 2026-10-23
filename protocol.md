# Wire Protocol

Binary request/response protocol used between the client and the key-value server.

## 1. Overview

| Property | Value |
|---|---|
| Transport | TCP (byte stream, so all messages are length-prefixed) |
| Default port | 1234 |
| Byte order | Little-endian for all integers |
| Max message body | 1 MiB (`1024 * 1024` bytes) |
| Ordering | One response per request, in request order |
| Pipelining | Supported (the server loops over all complete requests in its buffer) |

Because responses are returned strictly in request order, a client may send many requests back-to-back and then read the same number of responses.

## 2. Framing

Every message in both directions is one frame:

```
+----------------+---------------------------+
| u32 body_len   | body (body_len bytes)     |
+----------------+---------------------------+
```

- The receiver reads 4 bytes, then reads exactly `body_len` more bytes.
- If `body_len` exceeds the 1 MiB limit, the server closes the connection.
- If fewer than `4 + body_len` bytes have arrived, the server waits for more data.

## 3. Primitive types

| Type | Size | Encoding |
|---|---|---|
| `u8` | 1 byte | as is |
| `u32` | 4 bytes | little-endian, least significant byte first |
| `i64` | 8 bytes | two's complement, little-endian |
| `f64` | 8 bytes | IEEE 754 binary64 bit pattern, little-endian |

## 4. Request format

The body is a counted list of strings. The first string is the command name and the rest are its arguments.

```
+-----------+--------------------+--------------------+-----
| u32 nstr  | u32 len1 | bytes1  | u32 len2 | bytes2  | ...
+-----------+--------------------+--------------------+-----
```

| Field | Size | Meaning |
|---|---|---|
| `nstr` | 4 bytes | number of strings in the request (max 1 MiB) |
| `lenN` | 4 bytes | length of string N in bytes |
| `bytesN` | `lenN` bytes | raw content (binary-safe, not NUL-terminated) |

The server rejects a request and closes the connection if:

- any length field would read past the end of the body, or
- bytes remain after the last string, or
- `nstr` exceeds the limit.

### Example: `set a 1`

```
15 00 00 00              frame length = 21
03 00 00 00              nstr = 3
03 00 00 00  73 65 74    len 3, "set"
01 00 00 00  61          len 1, "a"
01 00 00 00  31          len 1, "1"
```

## 5. Response format

The body is a single tagged value (type byte first, then the payload).

| Tag | Value | Payload after the tag byte |
|---|---|---|
| `TAG_NIL` | 0 | none |
| `TAG_ERR` | 1 | `u32 code`, then a complete `TAG_STR` value |
| `TAG_STR` | 2 | `u32 len`, then `len` bytes |
| `TAG_INT` | 3 | `i64` |
| `TAG_DBL` | 4 | `f64` |
| `TAG_ARR` | 5 | `u32 count`, then `count` complete values |

### Notes

- **Errors nest a string.** The message is a full string value, so the layout is `[TAG_ERR][u32 code][TAG_STR][u32 len][bytes]`. The total size is `1 + 4 + 1 + 4 + len` bytes.
- **Arrays hold tagged values.** Each element carries its own tag, so arrays can contain any type, including other arrays. The server helper `out_arr` writes only the header (tag and count); the caller must then emit exactly `count` values.
- **Strings are binary-safe.** Length is explicit, so they may contain any byte.

### Examples

Each response is preceded by its 4-byte frame length.

**nil**

```
01 00 00 00 | 00
```

**string `"1"`**

```
06 00 00 00 | 02 01 00 00 00 31
```

**error 404, `"Key Doesn't exist"` (17 bytes)**

```
1B 00 00 00 | 01 94 01 00 00 02 11 00 00 00
              4B 65 79 20 44 6F 65 73 6E 27 74 20 65 78 69 73 74
```

**integer 42**

```
09 00 00 00 | 03 2A 00 00 00 00 00 00 00
```

**array of two integers (1, 2)**

```
17 00 00 00 | 05 02 00 00 00
              03 01 00 00 00 00 00 00 00
              03 02 00 00 00 00 00 00 00
```

## 6. Commands

| Request | Success response | Failure response |
|---|---|---|
| `get <key>` | `STR value` | `ERR 404 "Key Doesn't exist"` |
| `set <key> <value>` | `NIL` | none |
| `erase <key>` | `NIL` | none |
| any other command or wrong argument count | none | `ERR 404 "unrecognized command"` |

Command names are case-sensitive and lowercase.

## 7. Error codes

| Code | Meaning |
|---|---|
| 404 | Key not found, or unrecognized command |

## 8. Connection behaviour

The server closes the connection when:

- a frame length exceeds the 1 MiB limit,
- a request body is malformed,
- the peer closes its side or a socket error occurs.

There is no handshake, authentication, or version negotiation.

## 9. Implementation notes

- Both sides should encode and decode integers byte by byte rather than copying raw memory, so the protocol is independent of host endianness. The server's `append_u32` and `append_i64` already do this. The frame-length header in `make_response` and `try_one_request`, and the request decoder `read_u32`, should do the same.
- A reference client is in `client.cpp`. Run `./client --test` to exercise pipelining.