#define BPP_RECOMPILE_SELF_CMD "clang++ -g -O0 -w"
#include "buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;
    
    auto main = b->addExe({.name = "main", .desc = "My simple binary artefact"}, {"main.cpp"});
    b->installExe(main, "runme");
    b->addRun(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});

    auto libmain = b->addLib({.name = "main", .desc = "My simple binary artefact"/*, .link = StaticLinkTool{"gcc-ar"}*/}, {"main.cpp"});
    b->installLib(libmain, libmain->libName());
}
