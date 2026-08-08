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
    std::cout<<"Accepting connection, fd = "<<connfd<<std::endl;
    if (connfd < 0) {
        return NULL;
    }

    fd_set_nb(connfd);

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
    if (len > k_max_msg) { 
        conn->want_close = true;
        return false; 
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
    return true;      
}

 void handle_read(Conn *conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {  
        conn->want_close = true;
        return;
    }
    std::cout<<"Read "<<rv<<" bytes"<<std::endl;
  
    buf_append(conn->incoming, buf, (size_t)rv);
    while(try_one_request(conn));
    if (conn->outgoing.size() > 0) {    
        conn->want_read = false;
        conn->want_write = true;
        return handle_write(conn);
    }
}

 void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
     if (rv < 0) {
        conn->want_close = true;
        return;
    }

    buf_consume(conn->outgoing, (size_t)rv);
     if (conn->outgoing.size() == 0) {   // 2. Written 1 response.
        conn->want_read = true;         // 3. Wait for more data.
        conn->want_write = false;
    }
}
