#ifndef NETWORK_H
#define NETWORK_H

#include "connection.h"
#include<iostream>
#include<cstdint>
#include <vector>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <cstring>
#include <unistd.h>
#include <cassert>
#include <errno.h>
#include <cstdlib>
#include <cstdio>


Conn* handle_accept(int fd);
void handle_read(Conn* conn);
void handle_write(Conn* conn);

#endif