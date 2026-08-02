#ifndef CONNECTION_H
#define CONNECTION_H

#include <vector>
#include <cstdint>

struct Conn{
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    std::vector<uint8_t>incoming;
    std::vector<uint8_t>outgoing;
};

#endif