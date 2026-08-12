#include "myhashtable.h"

#include <iostream>
#include <stdexcept>


// ---------------------------------------------------------
// Constructor
// ---------------------------------------------------------

Hashtable::Hashtable(size_t capacity)
    : newer(capacity, nullptr)
{
    if (capacity == 0) {
        throw std::invalid_argument(
            "Hashtable capacity cannot be zero"
        );
    }
}


// ---------------------------------------------------------
// Destructor
// ---------------------------------------------------------

Hashtable::~Hashtable()
{
    destroy_table(newer);
    destroy_table(older);
}


// ---------------------------------------------------------
// Destroy all Nodes in a table
// ---------------------------------------------------------

void Hashtable::destroy_table(
    std::vector<Node*>& table
)
{
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

uint32_t Hashtable::hash(
    const std::string& key
) const
{
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

bool Hashtable::is_rehashing() const
{
    return !older.empty();
}

void Hashtable::start_resize()
{
    // Don't start another resize if one is already running
    if (is_rehashing()) {
        return;
    }

    // Move current table into older
    older = std::move(newer);

    // Create a table twice as large
    newer = std::vector<Node*>(
        older.size() * 2,
        nullptr
    );

    // Start migration from bucket 0
    migrate_pos = 0;
}

void Hashtable::help_rehashing()
{
    if (!is_rehashing()) {
        return;
    }

    constexpr size_t REHASH_WORK = 2;

    size_t work = 0;

    while (
        work < REHASH_WORK &&
        migrate_pos < older.size()
    ) {

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

    // Check whether all buckets have been migrated
    if (migrate_pos >= older.size()) {

        // All Nodes should already be moved
        older.clear();

        migrate_pos = 0;
    }
}


bool Hashtable::set(
    const std::string& key,
    const std::string& value
){

    help_rehashing();

    size_t new_index =
        hash(key) % newer.size();

    Node* curr = newer[new_index];

    while (curr != nullptr) {

        if (curr->key == key) {

            // Update existing value
            curr->value = value;

            return true;
        }

        curr = curr->next;
    }

    if (is_rehashing()) {

        size_t old_index =
            hash(key) % older.size();

        curr = older[old_index];

        while (curr != nullptr) {

            if (curr->key == key) {

                // Update existing value
                curr->value = value;

                return true;
            }

            curr = curr->next;
        }
    }

    Node* newNode =
        new Node(key, value);


    // New entries ALWAYS go to newer table
    newNode->next = newer[new_index];

    newer[new_index] = newNode;

    size++;

    if (
        !is_rehashing() &&
        load_factor() > MAX_LOAD_FACTOR
    ) {
        start_resize();
    }


    // Do a little migration after insertion
    help_rehashing();

    return true;
}

bool Hashtable::get(
    const std::string& key,
    std::string& value
){

    help_rehashing();

    size_t new_index =
        hash(key) % newer.size();

    Node* curr = newer[new_index];

    while (curr != nullptr) {

        if (curr->key == key) {

            value = curr->value;

            return true;
        }

        curr = curr->next;
    }

    if (is_rehashing()) {

        size_t old_index =
            hash(key) % older.size();

        curr = older[old_index];

        while (curr != nullptr) {

            if (curr->key == key) {

                value = curr->value;

                return true;
            }

            curr = curr->next;
        }
    }

    return false;
}

bool Hashtable::erase(
    const std::string& key
)
{
    // Progress migration
    help_rehashing();

    size_t new_index =
        hash(key) % newer.size();

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