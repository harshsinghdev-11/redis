#include "network.h"
#include "buffer.h"
#include "utils.h"
#include "myhashtable.h"
#include "serialisation.h"
#include "pubsub.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

using namespace std;

Hashtable g_data(30);
static const uint32_t k_max_msg = 1024 * 1024; 

Conn* handle_accept(int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    cout << "Accepting connection, fd = " << connfd << endl;
    if (connfd < 0) {
        return NULL;
    }

    fd_set_nb(connfd);

    Conn *conn = new Conn();
    conn->fd = connfd;
    cout << "Client connected. fd = " << conn->fd << endl;
    conn->want_read = true; // read the 1st request
    return conn;
}

int32_t parse_req(const uint8_t *data, size_t size, vector<string>& out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;

    if (!read_u32(data, end, nstr)) {
        return -1;
    }
    if (nstr > k_max_msg) {
        return -1;
    }

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) {
            return -1;
        }
        out.push_back(string());
        if (!read_str(data, end, len, out.back())) {
            return -1;
        }
    }

    if (data != end) {
        return -1;
    }
    return 0;
}

void do_request(vector<string> &cmd, tempBuffer &out, Conn *conn) {
    if (cmd.empty()) {
        return;
    }

    // Context enforcement: If connection is subscribed, only Pub/Sub & control commands allowed
    if (is_pubsub_mode(conn) && !is_pubsub_allowed_cmd(cmd[0])) {
        out_err(out, 400, "ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT allowed in this context");
        return;
    }

    // 1. SUBSCRIBE
    if (cmd[0] == "subscribe") {
        if (cmd.size() < 2) {
            out_err(out, 400, "ERR wrong number of arguments for 'subscribe' command");
            return;
        }
        vector<string> channels(cmd.begin() + 1, cmd.end());
        pubsub_subscribe(conn, channels, out);
        return;
    }

    // 2. UNSUBSCRIBE
    if (cmd[0] == "unsubscribe") {
        vector<string> channels;
        if (cmd.size() > 1) {
            channels.assign(cmd.begin() + 1, cmd.end());
        }
        pubsub_unsubscribe(conn, channels, out);
        return;
    }

    // 3. PUBLISH
    if (cmd[0] == "publish") {
        if (cmd.size() != 3) {
            out_err(out, 400, "ERR wrong number of arguments for 'publish' command");
            return;
        }
        int64_t receivers = pubsub_publish(cmd[1], cmd[2]);
        out_int(out, receivers);
        return;
    }

    // 4. PING
    if (cmd[0] == "ping") {
        if (is_pubsub_mode(conn)) {
            string payload = (cmd.size() > 1) ? cmd[1] : "";
            out_arr(out, 2);
            out_str(out, "pong");
            out_str(out, payload);
        } else {
            if (cmd.size() == 1) {
                out_str(out, "PONG");
            } else if (cmd.size() == 2) {
                out_str(out, cmd[1]);
            } else {
                out_err(out, 400, "ERR wrong number of arguments for 'ping' command");
            }
        }
        return;
    }

    // 5. QUIT
    if (cmd[0] == "quit") {
        out_nil(out);
        if (conn) {
            conn->want_close = true;
        }
        return;
    }

    // 6. GET
    if (cmd.size() == 2 && cmd[0] == "get") {
        string val = g_data.get(cmd[1]);
        if (val == "Key Doesn't exist") {
            out_err(out, 404, "Key Doesn't exist");
            return;
        }
        out_str(out, val);
        return;
    }

    // 7. SET
    if (cmd.size() == 3 && cmd[0] == "set") {
        g_data.set(cmd[1], cmd[2]);
        out_nil(out);
        return;
    }

    // 8. ERASE
    if (cmd.size() == 2 && cmd[0] == "erase") {
        if (g_data.erase(cmd[1])) {
            out_nil(out);
        } else {
            out_err(out, 404, "Key Doesn't exist");
        }
        return;
    }

    // Unrecognized command
    out_err(out, 404, "unrecognized command");
}

void make_response(const vector<uint8_t> &resp, vector<uint8_t> &outgoing) {
    uint32_t len = static_cast<uint32_t>(resp.size());
    const auto* header = reinterpret_cast<const uint8_t*>(&len);
    outgoing.insert(outgoing.end(), header, header + 4);
    outgoing.insert(outgoing.end(), resp.begin(), resp.end());
}

bool try_one_request(Conn *conn) {
    if (conn->incoming.size() < 4) {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) { 
        conn->want_close = true;
        return false; 
    }

    if (4 + len > conn->incoming.size()) {
        return false;
    }

    const uint8_t *request = &conn->incoming[4];
    vector<string> cmd;
    if (parse_req(request, len, cmd) < 0) {
        conn->want_close = true;
        return false;
    }

    vector<uint8_t> resp;
    do_request(cmd, resp, conn);
    if (!resp.empty()) {
        make_response(resp, conn->outgoing);
    }
    conn->incoming.erase(conn->incoming.begin(), conn->incoming.begin() + 4 + len);
    return true;      
}

void handle_read(Conn *conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {  
        conn->want_close = true;
        return;
    }

    buf_append(conn->incoming, buf, (size_t)rv);
    while (try_one_request(conn));

    if (conn->outgoing.size() > 0) {
        if (!is_pubsub_mode(conn)) {
            conn->want_read = false;
        }
        conn->want_write = true;
        return handle_write(conn);
    }
}

void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv <= 0) {
        conn->want_close = true;
        return;
    }
    size_t written_bytes = static_cast<size_t>(rv);
    conn->outgoing.erase(conn->outgoing.begin(), conn->outgoing.begin() + written_bytes);
    if (conn->outgoing.size() == 0) {  
        conn->want_read = true;
        conn->want_write = false;
    }
}
