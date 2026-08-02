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


#include "connection.h"
#include "utils.h"
#include "network.h"

using namespace std;



int main(){
    
    int fd = socket(AF_INET,SOCK_STREAM,0);
    std::cout<<"Connection fd is: ";
    std::cout<<fd<<"\n";
    if(fd<0){
        die("socket()");
    }

    int val = 1;
    // SOL_SOCKET = general socket option
    // second parameter of setsockopt suggest the layer of TCP/IP 
    setsockopt(fd,SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    std::cout<<"socket is configured\n";
    //binding the socket
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);


    //typecasting &addr - generic c api accepting the base struct 
    // a form of manual polymorphism
    int rv = bind(fd,(const sockaddr*)&addr,sizeof(addr));
    if(rv){
        die("bind()");
    }

    fd_set_nb(fd);


    //SOMAXCONN = maximum backlog queue
    // what is backlog queue? 
    rv = listen(fd,SOMAXCONN);
    if(rv){
        die("listen()");
    }

    vector<Conn*> fd2Conn;

    //
    vector<struct pollfd>poll_args;

    while(true){
        poll_args.clear();
        std::cout<<"event loop is started\n";

        //listening socket in first position
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
                std::cout<<"Client connected";
                fd2Conn[conn->fd] = conn;
            }
        }

        for(size_t i=1;i<poll_args.size();i++){
            uint32_t ready = poll_args[i].revents;
            if(ready==0){
                continue;
            }
            Conn *conn = fd2Conn[poll_args[i].fd];
            if(ready & POLLIN){
                assert(conn->want_read);
                handle_read(conn);
            }
            if(ready & POLLOUT){
                assert(conn->want_write);
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
