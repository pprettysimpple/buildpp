#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

void configure(Build* b) {
    auto main = b->addExe({.name = "main", .desc = "My simple executable"}, {"main.cpp"});
    b->installExe(main);
    b->addRunExe(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});

    auto libmain = b->addLib({.name = "main", .desc = "My simple library"}, {"main.cpp"});
    b->installLib(libmain);
}
