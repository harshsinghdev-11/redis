#ifndef CONNECTION_H
#define CONNECTION_H

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_set>

struct Conn {
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;

    // Active channel subscriptions for Pub/Sub
    std::unordered_set<std::string> channels;
};

#endif
