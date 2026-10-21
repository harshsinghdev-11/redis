#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <bits/stdc++.h>
#include <cstdint>
using namespace std;
using tempBuffer =  vector<uint8_t>;

enum : uint8_t{

    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5

};

void out_nil(tempBuffer& out);
void out_str(tempBuffer& out, const string& value);
void out_int(tempBuffer& out, int64_t value);
void out_dbl(tempBuffer& out, double value);
void out_arr(tempBuffer& out, uint32_t count);
void out_err(tempBuffer& out, uint32_t code,const string& message);


#endif