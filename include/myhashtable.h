#ifndef MYHASHTABLE
#define MYHASHTABLE
#include "linkedlist.h"
#include <vector>
#include <cstdint>


class Hashtable{
private:
    std::vector<Node*>hashtable;
    size_t slots = 0;

public:

    Hashtable(size_t capacity):hashtable(capacity,nullptr){}
    uint32_t hash(const std::string& key);

    bool set(const std::string& key, const std::string& value);

    std::string get(const std::string& key);

    bool erase(const std::string& key);

    double load_factor() const;

    
    
};

#endif