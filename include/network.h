#ifndef NETWORK_H
#define NETWORK_H

#include "connection.h"
#include "serialisation.h"
#include <iostream>
#include <cstdint>
#include <vector>
#include <string>
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
void make_response(const std::vector<uint8_t> &resp, std::vector<uint8_t> &outgoing);
void do_request(std::vector<std::string> &cmd, tempBuffer &out, Conn *conn);

#endif
