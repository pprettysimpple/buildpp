#include <iostream>

void foo() {
    #ifdef ENABLE_FEATURE_X
    std::cout << "Also, feature X is enabled!" << std::endl;
    #endif
}
