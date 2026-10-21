#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>
#include <iostream>


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

void append_u32(std::vector<uint8_t>&buffer,const uint32_t &len){
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&len);
    buffer.insert(buffer.end(),bytes,bytes+sizeof(len));
    return;
}

void append_string(std::vector<uint8_t>&buffer,const std::string &str){
    uint32_t len = static_cast<uint32_t>(str.size());
    append_u32(buffer,len);
    buffer.insert(buffer.end(),str.begin(),str.end());
}

//cmd contain commands
std::vector<uint8_t> make_request(const std::vector<std::string>&cmd){
    //request -> nstr , str.size , str
    std::vector<uint8_t>out;
    uint32_t nstr = static_cast<uint32_t>(cmd.size());
    append_u32(out,nstr);
    uint32_t bodySize = 0;
    size_t cmdSize = cmd.size();
    for(size_t i=0;i<cmdSize;i++){
        append_string(out,cmd[i]);
    }

    std::vector<uint8_t>request;
    uint32_t outSize = static_cast<uint32_t>(out.size());
    append_u32(request,outSize);
    request.insert(request.end(),out.begin(),out.end());
    return request;

}

void print_bytes(const std::vector<uint8_t>& buffer) {
    for (uint8_t byte : buffer) {
        std::cout << static_cast<int>(byte) << " ";
        // std::cout <<byte << " ";
    }

    std::cout << "\n";
     for (uint8_t byte : buffer) {
        // std::cout << static_cast<int>(byte) << " ";
        std::cout <<byte << " ";
    }
}

bool send_request(int fd,std::vector<uint8_t>&request){

    size_t total_sent =0;
    size_t request_size = request.size();
    while(total_sent<request_size){
        ssize_t rv = send(fd,request.data()+total_sent,request.size()-total_sent,0);
         if (rv < 0) {
            perror("send");
            return false;
        }
        total_sent += static_cast<size_t>(rv);
        std::cout<<"Send data: "<<total_sent<<std::endl;

    }
    
    return true;
}

bool receive_response(int fd) {
    uint8_t buffer[4096];

    ssize_t rv = recv(
        fd,
        buffer,
        sizeof(buffer),
        0
    );


    if (rv < 0) {
        return false;
    }

    std::cout << "Received "
              << rv
              << " bytes\n";

    for (ssize_t i = 0; i < rv; i++) {
        std::cout << buffer[i] << " ";
    }

    std::cout << "\n";
    return true;
}

int main() {
    int fd = create_socket();
    if(!connect_to_server(fd)){
        std::cout<<"Error in connecting to server";
    }
    std::vector<std::string>cmd = {"set","name","Harsh"};
     std::vector<uint8_t>request = make_request(cmd); 
    send_request(fd,request);
    while(!receive_response(fd)){}

    std::vector<std::string>cmd2 = {"get","name"};
    std::vector<uint8_t>request2 = make_request(cmd2);
    send_request(fd,request2);
    while(!receive_response(fd)){}
    return 0;
}