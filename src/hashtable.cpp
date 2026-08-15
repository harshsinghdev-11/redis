#include "myhashtable.h"

#include <iostream>
#include <stdexcept>

Hashtable::Hashtable(size_t capacity): newer(capacity, nullptr){
    if (capacity == 0) {
        throw std::invalid_argument(
            "Hashtable capacity cannot be zero"
        );
    }
}

Hashtable::~Hashtable(){
    destroy_table(newer);
    destroy_table(older);
}

void Hashtable::destroy_table(std::vector<Node*>& table){
    for (Node*& head : table) {

        Node* curr = head;

        while (curr != nullptr) {

            Node* next = curr->next;

            delete curr;

            curr = next;
        }

        head = nullptr;
    }
}

uint32_t Hashtable::hash(const std::string& key)
 const{
    uint32_t hash_value = 0x811c9dc5;

    for (unsigned char c : key) {

        hash_value ^= c;

        hash_value *= 0x01000193;
    }

    return hash_value;
}


double Hashtable::load_factor() const
{
    return static_cast<double>(size)
           / newer.size();
}

bool Hashtable::is_rehashing() const{
    return !older.empty();
}

void Hashtable::start_resize(){
    if (is_rehashing()) {
        return;
    }
    older = std::move(newer);
    newer = std::vector<Node*>(
        older.size() * 2,
        nullptr
    );
    migrate_pos = 0;
}

void Hashtable::help_rehashing(){
    if (!is_rehashing()) {
        return;
    }

    constexpr size_t REHASH_WORK = 2;

    size_t work = 0;

    while ( work < REHASH_WORK && migrate_pos < older.size()) {

        Node* curr = older[migrate_pos];

        // Empty bucket
        if (curr == nullptr) {
            migrate_pos++;
            continue;
        }

        older[migrate_pos] = curr->next;

        size_t new_index =
            hash(curr->key) % newer.size();

        curr->next = newer[new_index];

        newer[new_index] = curr;

        work++;
    }

    if (migrate_pos >= older.size()) {
        older.clear();
        migrate_pos = 0;
    }
}


bool Hashtable::set(const std::string& key,const std::string& value){

    help_rehashing();

    size_t new_index =
        hash(key) % newer.size();

    Node* curr = newer[new_index];

    while (curr != nullptr) {

        if (curr->key == key) {
            curr->value = value;

            return true;
        }

        curr = curr->next;
    }

    if (is_rehashing()) {
        size_t old_index = hash(key) % older.size();
        curr = older[old_index];
        while (curr != nullptr) {
            if (curr->key == key) {
                curr->value = value;

                return true;
            }
            curr = curr->next;
        }
    }

    Node* newNode =new Node(key, value);
    newNode->next = newer[new_index];

    newer[new_index] = newNode;

    size++;

    if (!is_rehashing() && load_factor() > MAX_LOAD_FACTOR) {
        start_resize();
    }
    help_rehashing();

    return true;
}

std::string Hashtable::get(const std::string& key
){
    help_rehashing();
    size_t new_index = hash(key) % newer.size();

    Node* curr = newer[new_index];
    std::string value = "Key Doesn't exist";
    while (curr != nullptr) {

        if (curr->key == key) {
           value = curr->value;
            return value;
        }

        curr = curr->next;
    }

    if (is_rehashing()) {

        size_t old_index = hash(key) % older.size();

        curr = older[old_index];
        while (curr != nullptr) {

            if (curr->key == key) {

                value = curr->value;

                return value;
            }

            curr = curr->next;
        }
    }

    return value;
}

bool Hashtable::erase(
    const std::string& key
)
{
    help_rehashing();

    size_t new_index = hash(key) % newer.size();

    Node* curr = newer[new_index];

    Node* prev = nullptr;

    while (curr != nullptr) {

        if (curr->key == key) {

            if (prev == nullptr) {
                newer[new_index] = curr->next;
            }
            else {
                prev->next = curr->next;
            }

            delete curr;

            size--;

            return true;
        }

        prev = curr;

        curr = curr->next;
    }

    if (is_rehashing()) {

        size_t old_index =
            hash(key) % older.size();

        curr = older[old_index];

        prev = nullptr;

        while (curr != nullptr) {

            if (curr->key == key) {

                if (prev == nullptr) {
                    older[old_index] = curr->next;
                }
                else {
                    prev->next = curr->next;
                }

                delete curr;

                size--;

                return true;
            }

            prev = curr;

            curr = curr->next;
        }
    }

    return false;
}