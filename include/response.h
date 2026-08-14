#ifndef RESPONSE
#define RESPONE

struct Response {
    uint32_t status = 0;
    std::vector<uint8_t> data;
};

#endif