#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>

void die(const char* msg){
    perror(msg);
    exit(1);
}

void fd_set_nb(int fd){
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0 | O_NONBLOCK));
}