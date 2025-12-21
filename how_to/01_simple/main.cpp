#include <iostream>

int main(int argc, char** argv) {
    std::cout << "Hello from example_01_simple1" << std::endl;
    for (int i = 1; i < argc; ++i) {
        std::cout << "Arg " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}
