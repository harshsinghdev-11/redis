#ifndef LINKED_LIST
#define LINKED_LIST
#include <string>

struct Node
{
    std::string key;
    std::string value;
    Node* next;
    Node(std::string key,std::string value){
        this->key = key;
        this->value = value;
        this->next = nullptr;
    }

};


#endif