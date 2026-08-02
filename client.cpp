#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    std::cout<<"\nhello";

    // Server address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);            // Same port as server
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    // Connect
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }
    while(true){

  std::string msg;
  std::cin>>msg;
    uint32_t len = msg.size();

    // Build packet = [4-byte length][message]
    std::vector<uint8_t> packet;
    packet.insert(packet.end(),
                  reinterpret_cast<uint8_t*>(&len),
                  reinterpret_cast<uint8_t*>(&len) + 4);
    packet.insert(packet.end(), msg.begin(), msg.end());

    // Send packet
    if (write(fd, packet.data(), packet.size()) != (ssize_t)packet.size()) {
        perror("write");
        return 1;
    }

    // Read reply header
    uint32_t replyLen;
    if (read(fd, &replyLen, 4) != 4) {
        perror("read header");
        return 1;
    }

    // Read reply body
    std::vector<char> buffer(replyLen + 1);
    if (read(fd, buffer.data(), replyLen) != (ssize_t)replyLen) {
        perror("read body");
        return 1;
    }

    buffer[replyLen] = '\0';

    std::cout << "Server replied: " << buffer.data() << std::endl;

    }
    // Message to send
  
    close(fd);
    return 0;
}