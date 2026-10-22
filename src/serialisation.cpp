#include "serialisation.h"
#include <bits/stdc++.h>
using namespace std;

static void append_u8(tempBuffer& out, uint8_t value) {
    out.push_back(value);
}

static void append_u32(tempBuffer& out, uint32_t value) {
    // Protocol uses little-endian.
    for (int i = 0; i < 4; ++i) {
        out.push_back(
            static_cast<uint8_t>((value >> (i * 8)) & 0xFF)
        );
    }
}

static void append_i64(tempBuffer& out, int64_t value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    for (int i = 0; i < 8; ++i) {
        out.push_back(
            static_cast<uint8_t>((bits >> (i * 8)) & 0xFF)
        );
    }
}

static void append_bytes(tempBuffer& out, const uint8_t* data, size_t size){
    out.insert(out.end(), data, data + size);
}

void out_nil(tempBuffer& out){
    append_u8(out, TAG_NIL);
}

void out_str(tempBuffer& out, const string& value){
    if (value.size() > UINT32_MAX) {
        throw std::length_error("String too large");
    }
    append_u8(out, TAG_STR);
    append_u32(out, static_cast<uint32_t>(value.size()));
    if (!value.empty()) {
        append_bytes(
            out,
            reinterpret_cast<const uint8_t*>(value.data()),
            value.size()
        );
    }
}



void out_int(tempBuffer& out, int64_t value){
     append_u8(out, TAG_INT);
    append_i64(out, value);
}

void out_dbl(tempBuffer& out, double value){
    static_assert(sizeof(double) == 8);

    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    append_u8(out, TAG_DBL);

    for (int i = 0; i < 8; ++i) {
        out.push_back(
            static_cast<uint8_t>((bits >> (i * 8)) & 0xFF)
        );
    }
}
void out_arr(tempBuffer& out, uint32_t count){
    append_u8(out, TAG_ARR);
    append_u32(out, count);
}

void out_err(tempBuffer& out, uint32_t code,const string& message){
    append_u8(out, TAG_ERR);
    append_u32(out, code);
    out_str(out, message);
}
