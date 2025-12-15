#define BPP_RECOMPILE_SELF_CMD "clang++ -g -O0 -w"
#include "buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;
    
    auto main = b->addExe({
        .name = "main", .desc = "My simple binary artefact",
        .obj = Flags{
            .asan = true,
            .debug_info = true,
            .warnings = false,
            .optimize = Optimize::O0
        },
    }, {"main.cpp"});
    b->addRun(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});
}
