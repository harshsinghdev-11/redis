#include<iostream>
#include<cstdint>
#include <vector>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <cstdlib>
#include <cstdio>

using namespace std;

static void die(const char *msg) {
    perror(msg);
    exit(1);
}


struct Conn{
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    vector<uint8_t>incoming;
    vector<uint8_t>outgoing;
};


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
    }
}
