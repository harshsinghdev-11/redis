#include <iostream>
#include "buffer.h"
using namespace std;


void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    size_t old_size = buf.size();
    buf.resize(old_size+len);
    for(size_t i=0;i<len;i++){
        buf[old_size+i] = data[i];
    }
}


void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}