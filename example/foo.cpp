#include <iostream>
#include <string>
#include <variant>

struct fooooo{
    std::variant<int, std::string, bool, double> data;
};

void foo() {
    #ifdef ENABLE_FEATURE_X
    std::cout << "Also, feature X is enabled!" << std::endl;
    #endif
}
