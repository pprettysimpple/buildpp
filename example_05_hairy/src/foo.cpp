#include "foobar.h"
#include <iostream>

void foo() {
    #if ENABLE_FEATURE_X
    std::cout << "Also, feature X is enabled in foo!" << std::endl;
    #endif
}
