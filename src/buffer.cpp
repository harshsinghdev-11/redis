#include <iostream>
#include "buffer.h"
using namespace std;


void buf_append(vector<uint8_t>& buf,const uint8_t* data,size_t len){
    buf.insert(buf.end(),data,data+len);
}

void buf_consume(vector<uint8_t>& buf,size_t n){
    buf.erase(buf.begin(),buf.begin()+n);
}