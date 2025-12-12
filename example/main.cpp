#include <iostream>
#include "foo.h"
#include <file.h> // installed by build.cpp

int main() {
    std::cout << "Hello, I'm built with buildpp!" << std::endl;
    foo();
    return 0;
}
