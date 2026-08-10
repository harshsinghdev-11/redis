#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <cstdint>
#include <string.h>
#include <string>

void die(const char* msg){
    perror(msg);
    exit(1);
}

void fd_set_nb(int fd){
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0 | O_NONBLOCK));
}

bool read_u32(const uint8_t *&cur, const uint8_t *end,uint32_t &out){
    if(cur+4>end){
        return false;
    }
    memcpy(&out,cur,4);
    cur+=4;
    return true;
}

bool read_str(const uint8_t *&curr,const uint8_t *end,size_t n,std::string &out){
    if(curr + n > end){
        return false;
    }
    out.assign(curr,curr+n);
    curr+=n;
    return true;
    
}   