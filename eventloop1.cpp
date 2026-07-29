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

using namespace std;

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static const uint32_t k_max_msg = 1024 * 1024; // 1 MiB max message size

struct Conn{
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    vector<uint8_t>incoming;
    vector<uint8_t>outgoing;
};

static Conn *handle_accept(int fd) {
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
    conn->want_read = true; // read the 1st request
    return conn;
}

static void fd_set_nb(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
};

static void
buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}
// remove from the front
static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

static bool try_one_request(Conn *conn) {
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
    // Protocol: message body
    if (4 + len > conn->incoming.size()) {
        return false;   // want read
    }
    const uint8_t *request = &conn->incoming[4];
    // 4. Process the parsed message.
    // ...
    // generate the response (echo)
    buf_append(conn->outgoing, (const uint8_t *)&len, 4);
    buf_append(conn->outgoing, request, len);
    // 5. Remove the message from `Conn::incoming`.
    buf_consume(conn->incoming, 4 + len);
    return true;        // success
}

static void handle_read(Conn *conn) {
    // 1. Do a non-blocking read.
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {  // handle IO error (rv < 0) or EOF (rv == 0)
        conn->want_close = true;
        return;
    }
    // 2. Add new data to the `Conn::incoming` buffer.
    buf_append(conn->incoming, buf, (size_t)rv);
    // 3. Try to parse the accumulated buffer.
    // 4. Process the parsed message.
    // 5. Remove the message from `Conn::incoming`.
    try_one_request(conn);
    // ...
}



static void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if (rv < 0) {
        conn->want_close = true;    // error handling
        return;
    }
    // remove written data from `outgoing`
    buf_consume(conn->outgoing, (size_t)rv);
    // ...
}

int main(){
    
    int fd = socket(AF_INET,SOCK_STREAM,0);
    vector<Conn*> fd2Conn;
    vector<struct pollfd>poll_args;

    while(true){
        poll_args.clear();
        struct pollfd pfd= {fd,POLLIN,0};
        poll_args.push_back(pfd);
        for(Conn *conn:fd2Conn){
            if(!conn){
                continue;
            }
            struct pollfd pfd = {conn->fd,POLLERR,0};
            if(conn->want_read){
                pfd.events |= POLLIN;
            }
            if(conn->want_write){
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(),(nfds_t)poll_args.size(),-1);
        if(rv<0 && errno==EINTR){
            continue;
        }
        if(rv<0){
            die("poll");
        }

        if(poll_args[0].revents){
            if(Conn *conn = handle_accept(fd)){
                if(fd2Conn.size() <= (size_t)conn->fd){
                    fd2Conn.resize(conn->fd+1);
                }
                fd2Conn[conn->fd] = conn;
            }
        }

        for(size_t i=1;i<poll_args.size();i++){
            uint32_t ready = poll_args[i].revents;
            Conn *conn = fd2Conn[poll_args[i].fd];
            if(ready & POLLIN){
                handle_read(conn);
            }
            if(ready & POLLOUT){
                handle_write(conn);
            }

            if((ready & POLLERR) || conn->want_close){
                (void)close(conn->fd);
                fd2Conn[conn->fd] = NULL;
                delete conn;
            }
        }


    }
}
