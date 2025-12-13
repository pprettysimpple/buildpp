#include <stdlib.h>

// run build on this example and fix it one line at a time to see how recompilation using this tool works

int main(int argc, char** argv) {
    char* a = (char*)malloc(10);
    *(a-1) = 0;
    // free(a);
}
