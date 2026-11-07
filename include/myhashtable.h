#ifndef MYHASHTABLE
#define MYHASHTABLE
#include "linkedlist.h"
#include <vector>
#include <cstdint>
#include<iostream>
using namespace std;

class Hashtable{
private:
   vector<Node*> newer;
    vector<Node*> older;
    size_t size = 0;
    size_t migrate_pos = 0;

    static constexpr double MAX_LOAD_FACTOR = 0.75;

    uint32_t hash(const string& key) const;

    void start_resize();

    void help_rehashing();

    void destroy_table(vector<Node*>& table);

    
public:


    explicit Hashtable(size_t capacity = 20);
    ~Hashtable();
    Hashtable(const Hashtable&) = delete;
    Hashtable& operator=(const Hashtable&) = delete;

    bool set(const string& key,const string& value);

    string get(const string& key);

    bool erase(const string& key);

    double load_factor() const;

    bool is_rehashing() const;
    
    
};

#endif