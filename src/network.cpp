#include "network.h"
#include "buffer.h"
#include "utils.h"
#include <response.h>
#include <iostream>
#include "myhashtable.h"
#include "serialisation.h"
#include <bits/stdc++.h>
using namespace std;

Hashtable g_data(30);
static const uint32_t k_max_msg = 1024 * 1024; 

Conn* handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    cout<<"Accepting connection, fd = "<<connfd<<endl;
    if (connfd < 0) {
        return NULL;
    }

    fd_set_nb(connfd);

    Conn *conn = new Conn();
    conn->fd = connfd;
    cout << "Client connected. fd = "<< conn->fd<< endl;
    conn->want_read = true; // read the 1st request
    return conn;
}

int32_t parse_req(const uint8_t *data,size_t size,vector<string>& out){

    //data -> points to first byte of request
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
        out.push_back(string());
        if(!read_str(data,end,len,out.back())){
            return -1;
        }
    }

    if(data!=end){
        return -1;
    }
    return 0;
}

void do_request(vector<string> &cmd, tempBuffer &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {

        string val = g_data.get(cmd[1]);
        if(val=="Key Doesn't exist"){
            string err_string = "Key Doesn't exist";
            out_err(out,404,err_string);
        }
        out_str(out,val);
        return;
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        g_data.set(cmd[1],cmd[2]);
        out_nil(out);
        return;
    } else if (cmd.size() == 2 && cmd[0] == "erase") {
        out_nil(out);
    } else {
        string err_string = "unrecognized command";
        out_err(out,404,err_string);       
    }
}


void make_response(vector<uint8_t> &resp, vector<uint8_t> &outgoing) {

    uint32_t len = static_cast<uint32_t>(resp.size());
    cout<<"response size after setting TAG : "<<resp.size()<<endl;
    cout<<"response size after setting TAG : "<<len<<endl;
    const auto* header = reinterpret_cast<const uint8_t*>(&len);
    outgoing.insert(outgoing.end(),header,header+4);
    outgoing.insert(outgoing.end(),resp.begin(),resp.end());
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
    cout<<"Message length = "<<len<<endl;
    // Protocol: message body
    if (4 + len > conn->incoming.size()) {
        return false;   // want read
    }
    const uint8_t *request = &conn->incoming[4];
    vector<string>cmd;
    if(parse_req(request,len,cmd)<0){
        conn->want_close = true;
        return false;
    }

    vector<uint8_t>resp;
    do_request(cmd,resp);
    make_response(resp,conn->outgoing);
    conn->incoming.erase(conn->incoming.begin(),conn->incoming.begin()+4+len);
    return true;      
}

 void handle_read(Conn *conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {  
        conn->want_close = true;
        return;
    }
    cout<<"Read "<<rv<<" bytes"<<endl;
  
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
     if (rv <= 0) {
        conn->want_close = true;
        return;
    }
    size_t written_bytes = static_cast<size_t>(rv);
    conn->outgoing.erase(conn->outgoing.begin(),conn->outgoing.begin()+written_bytes);
     if (conn->outgoing.size() == 0) {  
        conn->want_read = true;
        conn->want_write = false;
    }
}





