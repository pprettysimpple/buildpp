#define BPP_RECOMPILE_SELF_CMD "clang++ -g -O0 -w"
#include "buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;
    
    auto asan_flags = Flags{.extra_flags = "-fsanitize=address -g -O0 -w"}; // enabling asan, disabling warnings and optimizations for clarity
    auto main = b->addExecutable({
        .name = "main", .desc = "My simple binary artefact",
        .obj = asan_flags,
        .link = asan_flags, // enabling asan must be done for both compile and link steps
    }, {"main.cpp"});
    b->addRun(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});
}
