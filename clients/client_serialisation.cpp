#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

using namespace std;

static const uint32_t k_max_msg = 1024*1024;

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void die(const char *msg){
    perror(msg);
    exit(1);
}

int create_socket(){
    int fd = socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){
        perror("socket");
        return -1;
    }
    return fd;
}

bool connect_to_server(int fd){
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv == -1) {
        perror("connect");
        return false;
    }
    return rv==0;
}

void append_u32(vector<uint8_t>&out,const uint32_t &len){
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&len);
    out.insert(out.end(),bytes,bytes+sizeof(len));
    return;
}

void append_string(vector<uint8_t>&out,const string &str){
    uint32_t len = static_cast<uint32_t>(str.size());
    append_u32(out,len);
    cout<<"string length: "<<len<<endl;
    out.insert(out.end(),str.begin(),str.end());
    cout<<"string added"<<endl;
}

vector<uint8_t> make_request(const vector<string>&cmd){
    //request -> nstr , str.size , str
    vector<uint8_t>out;
    uint32_t nstr = static_cast<uint32_t>(cmd.size());
    cout<<"total string: "<<nstr<<endl;
    append_u32(out,nstr);
    uint32_t bodySize = 0;
    size_t cmdSize = cmd.size();
    for(size_t i=0;i<cmdSize;i++){
        append_string(out,cmd[i]);
    }

    vector<uint8_t>request;
    uint32_t outSize = static_cast<uint32_t>(out.size());
    append_u32(request,outSize);
    cout<<"total size: "<<outSize<<endl;
    request.insert(request.end(),out.begin(),out.end());
    return request;

}

bool send_request(int fd,vector<uint8_t>&request){
    size_t total_sent =0;
    size_t request_size = request.size();
    while(total_sent<request_size){
        ssize_t rv = send(fd,request.data()+total_sent,request.size()-total_sent,0);
         if (rv < 0) {
            perror("send");
            return false;
        }
        total_sent += static_cast<size_t>(rv);
        cout<<"Send data: "<<total_sent<<std::endl;

    }
    
    return true;
}

static int32_t read_full(int fd,uint8_t* buf,size_t n){
    while(n>0){
        ssize_t rv = read(fd,buf,n);
        if (rv < 0 && errno == EINTR) continue;
        if (rv <= 0) return -1;
        n-=(size_t)rv;
        buf+=rv;
    }
    return 0;
}

static uint32_t get_u32(const uint8_t *p) {
    // what is the other way to do this
    // does this work done by reinterpret ?
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}


static int32_t print_value(const uint8_t *d, size_t size, int indent = 0) {
    if (size < 1) return -1;
    string pad(indent, ' ');

    switch (d[0]) {
    case TAG_NIL:
        cout << pad << "(nil)\n";
        return 1;

    case TAG_ERR: {
        // [tag][u32 code][TAG_STR][u32 len][bytes]
        if (size < 1 + 4 + 1 + 4) return -1;
        uint32_t code = get_u32(d + 1);
        if (d[5] != TAG_STR) return -1;
        uint32_t len = get_u32(d + 6);
        if (size < (size_t)10 + len) return -1;
        cout << pad << "(err) [" << code << "] "
             << string((const char *)d + 10, len) << "\n";
        return 10 + (int32_t)len;
    }

    case TAG_STR: {
        if (size < 1 + 4) return -1;
        uint32_t len = get_u32(d + 1);
        if (size < (size_t)5 + len) return -1;
        cout << pad << "(str) \"" << string((const char *)d + 5, len) << "\"\n";
        return 5 + (int32_t)len;
    }

    case TAG_INT: {
        if (size < 1 + 8) return -1;
        uint64_t bits = get_u64(d + 1);
        int64_t v;
        memcpy(&v, &bits, 8);
        cout << pad << "(int) " << v << "\n";
        return 9;
    }

    case TAG_DBL: {
        if (size < 1 + 8) return -1;
        uint64_t bits = get_u64(d + 1);
        double v;
        memcpy(&v, &bits, 8);
        cout << pad << "(dbl) " << v << "\n";
        return 9;
    }

    case TAG_ARR: {
        // [tag][u32 count] followed by `count` values
        if (size < 1 + 4) return -1;
        uint32_t n = get_u32(d + 1);
        if (n > size) return -1;            // each value needs >= 1 byte
        cout << pad << "(arr) len=" << n << "\n";
        size_t pos = 5;
        for (uint32_t i = 0; i < n; i++) {
            int32_t rv = print_value(d + pos, size - pos, indent + 2);
            if (rv < 0) return -1;
            pos += (size_t)rv;
        }
        return (int32_t)pos;
    }

    default:
        cerr << "unknown tag " << (int)d[0] << " (check enum vs serialisation.h)\n";
        return -1;
    }
}

bool receive_response(int fd){
    uint8_t hdr[4];
    if(read_full(fd,hdr,4)<0){
        cout<<"server closed connection"<<endl;
        return false;
    }
    uint32_t len = get_u32(hdr);
    if(len>k_max_msg){
        cout<<"response too long"<<endl;
        return false;
    }
    vector<uint8_t>body(len);
    if(len>0 && read_full(fd,body.data(),len)<0){
        cout<<"read error"<<endl;
        return false;
    }

    size_t pos = 0;
    while(pos<len){
        int32_t rv = print_value(body.data()+pos,body.size()-pos);
        if(rv<0){
            cout<<"bad response"<<endl;
            return false;
        }
        pos+=(size_t)rv;
    }
    return true;

}

// bool receive_response(int fd){
//     uint8_t buffer[10000];
//     ssize_t rv = recv(fd,buffer,sizeof(buffer),0);
//     if(rv<0){
//         return false;
//     }

//     cout << "Received "<< rv<< " bytes"<<endl;
//     for(ssize_t i=0;i<rv;i++){
//         cout<<buffer[i]<<" ";
//     }
//     return true;
// }

int main(){

    int fd = create_socket();
    if(!connect_to_server(fd)){
        cout<<"error in connecting server"<<endl;
    }
    vector<string>cmd = {"set","name","Harsh"};
    vector<uint8_t>request = make_request(cmd);
    send_request(fd,request);
    vector<string>cmd2 = {"get","name"};
    vector<uint8_t>request2 = make_request(cmd2);
    send_request(fd,request2);
    while(receive_response(fd)){}

}