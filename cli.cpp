#include "kv.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc >= 3) {
        try {
            kv::Store db(argv[1]);
            const std::string_view command = argv[2];
            if (command == "set" && argc == 5) { db.set(argv[3], argv[4]); return 0; }
            if (command == "delete" && argc == 4) { db.erase(argv[3]); return 0; }
            if (command == "get" && argc == 4) {
                if (auto value = db.get(argv[3])) { std::cout << *value << '\n'; return 0; }
                return 2;
            }
            if (command == "compact" && argc == 3) { db.compact(); return 0; }
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    std::cerr << "Usage: kv_cli DIRECTORY set KEY VALUE | get KEY | delete KEY | compact\n";
    return 1;
}
