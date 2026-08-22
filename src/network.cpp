#include "network.h"
#include "buffer.h"
#include "utils.h"
#include <response.h>
#include <iostream>
#include "myhashtable.h"

Hashtable g_data(30);
static const uint32_t k_max_msg = 1024 * 1024; 

Conn* handle_accept(int fd) {
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

int32_t parse_req(const uint8_t *data,size_t size,std::vector<std::string>& out){

    const uint8_t *end = data+size;
    uint32_t nstr = 0;

    // read_u32 reads the first four bytes for getting the number of string
    if(!read_u32(data,end,nstr)){
        return -1;
    }
    if(nstr>k_max_msg){
        return -1;
    }

    while(out.size()<nstr){
        uint32_t len = 0;
        if(!read_u32(data,end,len)){
            return -1;
        }
        out.push_back(std::string());
        if(!read_str(data,end,len,out.back())){
            return -1;
        }
    }

    if(data!=end){
        return -1;
    }
    return 0;
}

void do_request(std::vector<std::string> &cmd, Response &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {

        std::string val = g_data.get(cmd[1]);
        if(val=="Key Doesn't exist"){
            out.status = 0;
            return;
        }
        out.data.assign(val.begin(), val.end());
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        g_data.set(cmd[1],cmd[2]);
    } else if (cmd.size() == 2 && cmd[0] == "erase") {
        g_data.erase(cmd[1]);
    } else {
        // unrecognized command
        out.status = -1;       
    }
}


void make_response(const Response &resp, std::vector<uint8_t> &out) {
    uint32_t resp_len = 4 + (uint32_t)resp.data.size();
    buf_append(out, (const uint8_t *)&resp_len, 4);
    buf_append(out, (const uint8_t *)&resp.status, 4);
    buf_append(out, resp.data.data(), resp.data.size());
}


bool try_one_request(Conn *conn) {
    //5
    if (conn->incoming.size() < 4) {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) { 
        conn->want_close = true;
        return false; 
    }
    std::cout<<"Message length = "<<len<<std::endl;

    //it should always return false
    if (4 + len > conn->incoming.size()) {
        return false;
    }
    const uint8_t *request = &conn->incoming[4];
    std::vector<std::string>cmd;
    if(parse_req(request,len,cmd)<0){
        conn->want_close = true;
        return false;
    }

    Response resp;
    do_request(cmd,resp);
    make_response(resp,conn->outgoing);

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
     if (conn->outgoing.size() == 0) {  
        conn->want_read = true;
        conn->want_write = false;
    }
}

