#include <iostream>
#include "foobar.h"

#include <file.h> // installed by build.cpp

int main() {
    std::cout << "Hello, I'm built with buildpp!" << std::endl;
    foo();
    bar();
    std::cout << "Flag from codegen: '" << flag << "'" << std::endl;
    return 0;
}
