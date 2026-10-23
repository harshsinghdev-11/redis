// client.cpp - interactive client for the custom Redis-like server
//
// Build:  g++ -std=c++17 -Wall -Wextra -O2 client.cpp -o client
// Run:    ./client [ip] [port]          (defaults: 127.0.0.1 1234)
//         ./client [ip] [port] --test   (runs a pipelining test and exits)
//
// Wire protocol (all integers little-endian):
//   request : [u32 len][u32 nstr]([u32 slen][bytes])*
//   response: [u32 len][value]*
//   values  : NIL  = [0]
//             ERR  = [1][u32 code][STR value]
//             STR  = [2][u32 len][bytes]
//             INT  = [3][i64]
//             DBL  = [4][f64]
//             ARR  = [5][u32 count] followed by `count` values

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

static const uint32_t k_max_msg = 1024 * 1024;


enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

// ---------------------------------------------------------------------------
// little-endian helpers
// ---------------------------------------------------------------------------
static void put_u32(vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)((v >> (i * 8)) & 0xFF));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

// ---------------------------------------------------------------------------
// blocking socket IO
// ---------------------------------------------------------------------------
static int32_t read_full(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;             // error or EOF
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// request encoding
// ---------------------------------------------------------------------------
static int32_t send_req(int fd, const vector<string> &cmd) {
    uint64_t len = 4;                       // for nstr
    for (const string &s : cmd) len += 4 + (uint64_t)s.size();
    if (len > k_max_msg) {
        cerr << "request too long\n";
        return -1;
    }

    vector<uint8_t> w;
    w.reserve(4 + (size_t)len);
    put_u32(w, (uint32_t)len);
    put_u32(w, (uint32_t)cmd.size());
    for (const string &s : cmd) {
        put_u32(w, (uint32_t)s.size());
        w.insert(w.end(), s.begin(), s.end());
    }
    return write_all(fd, w.data(), w.size());
}

// ---------------------------------------------------------------------------
// response decoding
// Prints one value; returns bytes consumed, or -1 if malformed/truncated.
// ---------------------------------------------------------------------------
static int32_t print_value(const uint8_t *d, size_t size, int indent = 0) {
    if (size < 1) return -1;
    string pad(indent, ' ');

    switch (d[0]) {
    case TAG_NIL:
        cout << pad << "(nil)\n";
        return 1;

    case TAG_ERR: {
        // [tag][u32 code][TAG_STR][u32 len][bytes]
        if (size < 1 + 4 + 1 + 4) return -1;
        uint32_t code = get_u32(d + 1);
        if (d[5] != TAG_STR) return -1;
        uint32_t len = get_u32(d + 6);
        if (size < (size_t)10 + len) return -1;
        cout << pad << "(err) [" << code << "] "
             << string((const char *)d + 10, len) << "\n";
        return 10 + (int32_t)len;
    }

    case TAG_STR: {
        if (size < 1 + 4) return -1;
        uint32_t len = get_u32(d + 1);
        if (size < (size_t)5 + len) return -1;
        cout << pad << "(str) \"" << string((const char *)d + 5, len) << "\"\n";
        return 5 + (int32_t)len;
    }

    case TAG_INT: {
        if (size < 1 + 8) return -1;
        uint64_t bits = get_u64(d + 1);
        int64_t v;
        memcpy(&v, &bits, 8);
        cout << pad << "(int) " << v << "\n";
        return 9;
    }

    case TAG_DBL: {
        if (size < 1 + 8) return -1;
        uint64_t bits = get_u64(d + 1);
        double v;
        memcpy(&v, &bits, 8);
        cout << pad << "(dbl) " << v << "\n";
        return 9;
    }

    case TAG_ARR: {
        // [tag][u32 count] followed by `count` values
        if (size < 1 + 4) return -1;
        uint32_t n = get_u32(d + 1);
        if (n > size) return -1;            // each value needs >= 1 byte
        cout << pad << "(arr) len=" << n << "\n";
        size_t pos = 5;
        for (uint32_t i = 0; i < n; i++) {
            int32_t rv = print_value(d + pos, size - pos, indent + 2);
            if (rv < 0) return -1;
            pos += (size_t)rv;
        }
        return (int32_t)pos;
    }

    default:
        cerr << "unknown tag " << (int)d[0] << " (check enum vs serialisation.h)\n";
        return -1;
    }
}

static int32_t read_res(int fd) {
    uint8_t hdr[4];
    if (read_full(fd, hdr, 4) < 0) {
        cerr << "server closed connection / read error\n";
        return -1;
    }
    uint32_t len = get_u32(hdr);
    if (len > k_max_msg) {
        cerr << "response too long\n";
        return -1;
    }

    vector<uint8_t> body(len);
    if (len > 0 && read_full(fd, body.data(), len) < 0) {
        cerr << "read error\n";
        return -1;
    }

    size_t pos = 0;
    while (pos < body.size()) {             // a frame may hold several values
        int32_t rv = print_value(body.data() + pos, body.size() - pos);
        if (rv < 0) {
            cerr << "bad response\n";
            return -1;
        }
        pos += (size_t)rv;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// input parsing: whitespace-separated tokens, "double quotes" allow spaces,
// backslash escapes \" and \\ inside quotes.
// ---------------------------------------------------------------------------
static bool tokenize(const string &line, vector<string> &out) {
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && isspace((unsigned char)line[i])) i++;
        if (i >= n) break;

        string tok;
        if (line[i] == '"') {
            i++;
            bool closed = false;
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n) {
                    tok.push_back(line[i + 1]);
                    i += 2;
                } else if (line[i] == '"') {
                    closed = true;
                    i++;
                    break;
                } else {
                    tok.push_back(line[i++]);
                }
            }
            if (!closed) return false;      // unterminated quote
        } else {
            while (i < n && !isspace((unsigned char)line[i])) tok.push_back(line[i++]);
        }
        out.push_back(tok);
    }
    return true;
}

// ---------------------------------------------------------------------------
// pipelining test: send everything first, then read all replies.
// Exercises the server's `while (try_one_request(conn))` loop.
// ---------------------------------------------------------------------------
static int pipeline_test(int fd) {
    vector<vector<string>> cmds = {
        {"set", "a", "1"},
        {"set", "b", "2"},
        {"get", "a"},
        {"get", "zzz"},
        {"erase", "a"},
        {"bogus"},
    };
    cout << "Sending " << cmds.size() << " requests back-to-back...\n";
    for (auto &c : cmds) {
        if (send_req(fd, c) < 0) {
            cerr << "write error\n";
            return 1;
        }
    }
    for (size_t i = 0; i < cmds.size(); i++) {
        cout << "reply " << i + 1 << ": ";
        if (read_res(fd) < 0) return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);               // get EPIPE instead of dying

    const char *ip = "127.0.0.1";
    int port = 1234;
    bool test_mode = false;

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            test_mode = true;
        } else if (positional == 0) {
            ip = argv[i];
            positional++;
        } else if (positional == 1) {
            port = atoi(argv[i]);
            positional++;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        cerr << "invalid ip: " << ip << "\n";
        return 1;
    }
    if (connect(fd, (const sockaddr *)&addr, sizeof(addr)) < 0) die("connect()");

    if (test_mode) {
        int rc = pipeline_test(fd);
        close(fd);
        return rc;
    }

    cout << "Connected to " << ip << ":" << port << "\n"
         << "Commands: set <k> <v> | get <k> | erase <k> | quit\n"
         << "Use \"double quotes\" for values containing spaces.\n";

    string line;
    while (true) {
        cout << "> " << flush;
        if (!getline(cin, line)) break;

        vector<string> cmd;
        if (!tokenize(line, cmd)) {
            cerr << "unterminated quote\n";
            continue;
        }
        if (cmd.empty()) continue;
        if (cmd[0] == "quit" || cmd[0] == "exit") break;

        if (send_req(fd, cmd) < 0) {
            cerr << "write error\n";
            break;
        }
        if (read_res(fd) < 0) break;
    }

    close(fd);
    return 0;
}