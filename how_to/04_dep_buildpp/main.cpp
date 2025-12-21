#include <iostream>
#include <foo.h>

int main(int argc, char** argv) {
    std::cout << "Hello, Buildpp Dependency Example!" << std::endl;
    std::cout << "foo() returned: " << foo() << std::endl;
    return 0;
}
