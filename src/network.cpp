#include "network.h"
#include "buffer.h"
#include "utils.h"
#include <iostream>
static const uint32_t k_max_msg = 1024 * 1024; 

Conn *handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        return NULL;
    }
    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);
    // create a `struct Conn`
    Conn *conn = new Conn();
    conn->fd = connfd;
    std::cout << "Client connected. fd = "<< conn->fd<< std::endl;
    conn->want_read = true; // read the 1st request
    return conn;
}

bool try_one_request(Conn *conn) {
    // 3. Try to parse the accumulated buffer.
    // Protocol: message header
    if (conn->incoming.size() < 4) {
        return false;   // want read
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {  // protocol error
        conn->want_close = true;
        return false;   // want close
    }
    std::cout<<"Message length = "<<len<<std::endl;
    // Protocol: message body
    if (4 + len > conn->incoming.size()) {
        return false;   // want read
    }
    const uint8_t *request = &conn->incoming[4];
    buf_append(conn->outgoing, (const uint8_t *)&len, 4);
    buf_append(conn->outgoing, request, len);

    buf_consume(conn->incoming, 4 + len);
    return true;        // success
}

 void handle_read(Conn *conn) {
    // 1. Do a non-blocking read.
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {  // handle IO error (rv < 0) or EOF (rv == 0)
        conn->want_close = true;
        return;
    }
    std::cout<<"Read"<<rv<<" bytes"<<std::endl;
    // 2. Add new data to the `Conn::incoming` buffer.
    buf_append(conn->incoming, buf, (size_t)rv);
    // 3. Try to parse the accumulated buffer.
    // 4. Process the parsed message.
    // 5. Remove the message from `Conn::incoming`.
    try_one_request(conn);
    if (conn->outgoing.size() > 0) {    
        conn->want_read = false;
        conn->want_write = true;
    }
}

 void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if (rv < 0) {
        conn->want_close = true;    // error handling
        return;
    }
    // remove written data from `outgoing`
    buf_consume(conn->outgoing, (size_t)rv);
     if (conn->outgoing.size() == 0) {   // 2. Written 1 response.
        conn->want_read = true;         // 3. Wait for more data.
        conn->want_write = false;
    }
}
