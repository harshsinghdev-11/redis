#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include "linkedlist.h"
#include "myhashtable.h"


// fnv1a_32
uint32_t Hashtable::hash(const std::string& key) {

    uint32_t hash = 0x811c9dc5;
    for (unsigned char c : key) {
        hash ^= c;
        hash *= 0x01000193;
    }
    return hash;
}

bool Hashtable::set(const std::string& key,const std::string& value){
    size_t index = hash(key) % hashtable.size();
    Node* newNode =  new Node(key,value);   
     Node* tempNode = hashtable[index];
        while(tempNode!=nullptr){
            if(tempNode->key==key){
                return false;
            }
            tempNode = tempNode->next;
        }
        newNode->next = hashtable[index];
        hashtable[index] = newNode;
    slots+=1;
    return true;

}

std::string Hashtable::get(const std::string &key){
    
    uint32_t hash_index = hash(key) % hashtable.size();
    Node* tempNode = hashtable[hash_index];
    while(tempNode){
        if(tempNode->key==key){
            return tempNode->value;
        }
        tempNode=tempNode->next;
    }
    return "No key exist";
}

bool Hashtable::erase(const std::string &key){
    uint32_t hash_index = hash(key) % hashtable.size();
    if(hashtable[hash_index]==nullptr){
        return -1;
    }
    Node* prev = nullptr;
    Node* curr = hashtable[hash_index];
    while(curr){
        if(curr->key==key){
            if(prev == nullptr){
                hashtable[hash_index] = curr->next;
                
            }else{
                prev->next = curr->next;
                
            }
            delete curr;
            slots-=1;
                return 1;
        }
        prev = curr;
        curr=curr->next;
    }
    return 0;

}

double Hashtable::load_factor() const {
    return static_cast<double>(slots) / hashtable.size();
}


