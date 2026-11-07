#ifndef LINKED_LIST
#define LINKED_LIST
#include <string>
using namespace std;
struct Node
{
    string key;
    string value;
    Node* next;
    Node(string key,string value){
        this->key = key;
        this->value = value;
        this->next = nullptr;
    }

};


#endif