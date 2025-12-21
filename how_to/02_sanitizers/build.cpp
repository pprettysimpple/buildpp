#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;
    
    auto main = b->addExe({
        .name = "main", .desc = "My simple binary artefact",
        .obj = {
            .warnings = false,
            .optimize = Optimize::O0
        },
        .exe_flags = {
            .asan = true,
            .debug_info = true,
            .lto = true,
        },
    }, {"main.cpp"});
    b->addRunExe(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});
}
