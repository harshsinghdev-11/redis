#ifndef UTILS_H
#define UTILS_H
#include <cstdint>
#include <iostream>
#include <string>

void die(const char* msg);
void fd_set_nb(int fd);
bool read_u32(const uint8_t *&cur, const uint8_t *end,uint32_t &out);
bool read_str(const uint8_t *&curr,const uint8_t *end,size_t n,std::string &out);


#endif