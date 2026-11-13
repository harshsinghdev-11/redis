#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

static const char* SERVER_IP = "127.0.0.1";
static const uint16_t SERVER_PORT = 1234;
static volatile sig_atomic_t g_running = 1;

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void sig_handler(int) {
    g_running = 0;
}

static void append_u32(vector<uint8_t>& out, uint32_t val) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t get_i64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (static_cast<uint64_t>(p[i])) << (i * 8);
    }
    int64_t res;
    memcpy(&res, &v, 8);
    return res;
}

static int32_t read_full(int fd, uint8_t* buf, size_t n) {
    while (n > 0 && g_running) {
        ssize_t rv = read(fd, buf, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;
        n -= static_cast<size_t>(rv);
        buf += rv;
    }
    return g_running ? 0 : -1;
}

vector<uint8_t> make_request(const vector<string>& cmd) {
    vector<uint8_t> body;
    append_u32(body, static_cast<uint32_t>(cmd.size()));
    for (const string &s : cmd) {
        append_u32(body, static_cast<uint32_t>(s.size()));
        body.insert(body.end(), s.begin(), s.end());
    }

    vector<uint8_t> frame;
    append_u32(frame, static_cast<uint32_t>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

bool send_request(int fd, const vector<uint8_t>& req) {
    size_t total = 0;
    while (total < req.size() && g_running) {
        ssize_t n = send(fd, req.data() + total, req.size() - total, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

bool read_frame(int fd, vector<uint8_t>& out_body) {
    uint8_t hdr[4];
    if (read_full(fd, hdr, 4) < 0) {
        return false;
    }
    uint32_t body_len = get_u32(hdr);
    if (body_len > 1024 * 1024) {
        cerr << "Error: frame length exceeds 1 MB safety limit\n";
        return false;
    }
    out_body.resize(body_len);
    if (body_len > 0 && read_full(fd, out_body.data(), body_len) < 0) {
        return false;
    }
    return true;
}

// Parses a string value starting at offset 'pos'. Returns bytes consumed, or -1 on error.
static int32_t parse_str(const uint8_t *data, size_t size, size_t pos, string &out_str) {
    if (pos + 5 > size) return -1;
    if (data[pos] != TAG_STR) return -1;
    uint32_t len = get_u32(data + pos + 1);
    if (pos + 5 + len > size) return -1;
    out_str.assign(reinterpret_cast<const char*>(data + pos + 5), len);
    return 5 + static_cast<int32_t>(len);
}

// Dispatches and renders incoming tagged array responses
void handle_array_message(const uint8_t *data, size_t size) {
    if (size < 5) return;
    uint32_t count = get_u32(data + 1);
    size_t pos = 5;

    vector<string> str_items;
    int64_t int_val = 0;
    bool has_int = false;

    for (uint32_t i = 0; i < count; i++) {
        if (pos >= size) break;
        uint8_t tag = data[pos];
        if (tag == TAG_STR) {
            string s;
            int32_t consumed = parse_str(data, size, pos, s);
            if (consumed < 0) break;
            str_items.push_back(s);
            pos += static_cast<size_t>(consumed);
        } else if (tag == TAG_INT) {
            if (pos + 9 > size) break;
            int_val = get_i64(data + pos + 1);
            has_int = true;
            pos += 9;
        } else {
            break;
        }
    }

    // 1. PUSHED MESSAGE: ["message", "<channel>", "<payload>"]
    if (str_items.size() == 3 && str_items[0] == "message") {
        cout << "\033[1;32m[RECEIVED]\033[0m Channel: \033[1;36m" << str_items[1]
             << "\033[0m | Message: \033[1;37m" << str_items[2] << "\033[0m" << endl;
        return;
    }

    // 2. SUBSCRIBE CONFIRMATION: ["subscribe", "<channel>", count]
    if (str_items.size() >= 2 && str_items[0] == "subscribe" && has_int) {
        cout << "\033[1;34m[SUBSCRIBED]\033[0m Channel: \033[1;36m" << str_items[1]
             << "\033[0m (Total active subscriptions: " << int_val << ")" << endl;
        return;
    }

    // 3. UNSUBSCRIBE CONFIRMATION: ["unsubscribe", "<channel>", remaining]
    if (str_items.size() >= 2 && str_items[0] == "unsubscribe" && has_int) {
        cout << "\033[1;33m[UNSUBSCRIBED]\033[0m Channel: \033[1;36m" << str_items[1]
             << "\033[0m (Remaining active subscriptions: " << int_val << ")" << endl;
        return;
    }

    // Generic array fallback
    cout << "(array) len=" << count << endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Collect channels from command line or default to "news"
    vector<string> channels;
    for (int i = 1; i < argc; i++) {
        channels.push_back(argv[i]);
    }
    if (channels.empty()) {
        channels.push_back("news");
    }

    cout << "=================================================\n";
    cout << "           Redis C++ Pub/Sub Subscriber          \n";
    cout << "=================================================\n";
    cout << "Connecting to " << SERVER_IP << ":" << SERVER_PORT << "...\n";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect failed");
        close(fd);
        return 1;
    }

    cout << "Connected! Subscribing to channel(s): ";
    for (size_t i = 0; i < channels.size(); i++) {
        cout << "'" << channels[i] << "'" << (i + 1 < channels.size() ? ", " : "");
    }
    cout << "\nPress Ctrl+C to exit.\n-------------------------------------------------\n";

    // Send SUBSCRIBE command
    vector<string> cmd = {"subscribe"};
    cmd.insert(cmd.end(), channels.begin(), channels.end());
    vector<uint8_t> req = make_request(cmd);
    if (!send_request(fd, req)) {
        cerr << "Failed to send subscribe request\n";
        close(fd);
        return 1;
    }

    // Continuous listening loop for incoming frames
    while (g_running) {
        vector<uint8_t> frame;
        if (!read_frame(fd, frame)) {
            if (!g_running) break;
            cout << "\nConnection closed by server.\n";
            break;
        }

        if (frame.empty()) continue;

        uint8_t tag = frame[0];
        if (tag == TAG_ARR) {
            handle_array_message(frame.data(), frame.size());
        } else if (tag == TAG_ERR) {
            if (frame.size() >= 10) {
                uint32_t code = get_u32(frame.data() + 1);
                string msg;
                parse_str(frame.data(), frame.size(), 5, msg);
                cerr << "\033[1;31m[ERROR " << code << "]\033[0m " << msg << endl;
            }
        } else {
            cout << "[RAW FRAME] Tag: " << static_cast<int>(tag) << " (" << frame.size() << " bytes)\n";
        }
    }

    cout << "\nShutting down subscriber...\n";
    close(fd);
    return 0;
}

