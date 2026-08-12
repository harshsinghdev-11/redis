#include "myhashtable.h"

#include <iostream>
#include <string>

int main()
{
    Hashtable table(4);

    table.set("name", "Harsh");
    table.set("age", "22");
    table.set("city", "Noida");

    std::string value;

    if (table.get("name", value)) {
        std::cout << "name = "
                  << value
                  << '\n';
    }

    if (table.get("age", value)) {
        std::cout << "age = "
                  << value
                  << '\n';
    }

    table.erase("age");

    if (!table.get("age", value)) {
        std::cout << "age not found\n";
    }

    std::cout << "Load factor = "
              << table.load_factor()
              << '\n';

    return 0;
}