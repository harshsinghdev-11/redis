#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <cstdint>

struct Buffer {
    uint8_t *buffer_begin;
    uint8_t *buffer_end;
    uint8_t *data_begin;
    uint8_t *data_end;
};

void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) ;

void buf_consume(std::vector<uint8_t> &buf, size_t n);

#endif