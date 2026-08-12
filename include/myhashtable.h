#ifndef MYHASHTABLE
#define MYHASHTABLE
#include "linkedlist.h"
#include <vector>
#include <cstdint>


class Hashtable{
private:
   std::vector<Node*> newer;
    std::vector<Node*> older;
    size_t size = 0;
    size_t migrate_pos = 0;

    static constexpr double MAX_LOAD_FACTOR = 0.75;

    uint32_t hash(const std::string& key) const;

    void start_resize();

    void help_rehashing();

    void destroy_table(std::vector<Node*>& table);

    
public:


    explicit Hashtable(size_t capacity = 20);
    ~Hashtable();
    Hashtable(const Hashtable&) = delete;
    Hashtable& operator=(const Hashtable&) = delete;

    bool set(const std::string& key,const std::string& value);

    bool get(const std::string& key,std::string& value);

    bool erase(const std::string& key);

    double load_factor() const;

    bool is_rehashing() const;
    
    
};

#endif