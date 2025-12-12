#include <iostream>

void bar() {
    #ifdef ENABLE_FEATURE_X
    std::cout << "Also, feature X is enabled!" << std::endl;
    #endif
}
