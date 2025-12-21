#include "foobar.h"
#include <iostream>

void bar() {
    #if ENABLE_FEATURE_X
    std::cout << "Also, feature X is enabled in bar!" << std::endl;
    #endif
}
