#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

static const char* SERVER_IP = "127.0.0.1";
static const uint16_t SERVER_PORT = 1234;

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

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
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;
        n -= static_cast<size_t>(rv);
        buf += rv;
    }
    return 0;
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
    while (total < req.size()) {
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

int connect_to_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect failed");
        close(fd);
        return -1;
    }
    return fd;
}

bool publish_message(int fd, const string& channel, const string& message) {
    vector<string> cmd = {"publish", channel, message};
    vector<uint8_t> req = make_request(cmd);
    if (!send_request(fd, req)) {
        cerr << "Failed to send request\n";
        return false;
    }

    vector<uint8_t> resp;
    if (!read_frame(fd, resp)) {
        cerr << "Failed to receive response from server\n";
        return false;
    }

    if (resp.empty()) {
        cerr << "Received empty response frame\n";
        return false;
    }

    if (resp[0] == TAG_INT) {
        if (resp.size() >= 9) {
            int64_t receivers = get_i64(resp.data() + 1);
            cout << "\033[1;32m[PUBLISHED]\033[0m Channel: \033[1;36m" << channel
                 << "\033[0m | Message: \033[1;37m\"" << message << "\"\033[0m | Receivers: \033[1;33m"
                 << receivers << "\033[0m\n";
            return true;
        }
    } else if (resp[0] == TAG_ERR) {
        if (resp.size() >= 10) {
            uint32_t code = get_u32(resp.data() + 1);
            uint32_t len = get_u32(resp.data() + 6);
            string err_msg(reinterpret_cast<const char*>(resp.data() + 10), len);
            cerr << "\033[1;31m[ERROR " << code << "]\033[0m " << err_msg << "\n";
            return false;
        }
    }

    cout << "Unexpected response tag: " << static_cast<int>(resp[0]) << "\n";
    return false;
}

int main(int argc, char* argv[]) {
    // Mode 1: One-shot command line arguments: ./publisher <channel> <message>
    if (argc >= 3) {
        string channel = argv[1];
        string message = argv[2];
        for (int i = 3; i < argc; i++) {
            message += " ";
            message += argv[i];
        }

        int fd = connect_to_server();
        if (fd < 0) return 1;

        bool ok = publish_message(fd, channel, message);
        close(fd);
        return ok ? 0 : 1;
    }

    // Mode 2: Interactive REPL Shell
    cout << "=================================================\n";
    cout << "           Redis C++ Pub/Sub Publisher           \n";
    cout << "=================================================\n";
    cout << "Connecting to " << SERVER_IP << ":" << SERVER_PORT << "...\n";

    int fd = connect_to_server();
    if (fd < 0) return 1;

    cout << "Connected!\n";
    cout << "Usage: <channel> <message>\n";
    cout << "Example: news Hello everyone!\n";
    cout << "Type 'exit' or 'quit' to disconnect.\n";
    cout << "-------------------------------------------------\n";

    string line;
    while (true) {
        cout << "\033[1;34mpub>\033[0m ";
        if (!getline(cin, line)) {
            break;
        }

        if (line.empty()) continue;
        if (line == "exit" || line == "quit") {
            break;
        }

        stringstream ss(line);
        string channel;
        ss >> channel;
        string message;
        getline(ss, message);

        // Trim leading space from message
        size_t start = message.find_first_not_of(" \t");
        if (start != string::npos) {
            message = message.substr(start);
        } else {
            message = "";
        }

        if (channel.empty()) {
            cout << "Please specify a channel.\n";
            continue;
        }

        if (message.empty()) {
            cout << "Please provide a message to publish.\n";
            continue;
        }

        if (!publish_message(fd, channel, message)) {
            cerr << "Connection error. Reconnecting...\n";
            close(fd);
            fd = connect_to_server();
            if (fd < 0) break;
        }
    }

    cout << "\nDisconnecting publisher...\n";
    close(fd);
    return 0;
}

