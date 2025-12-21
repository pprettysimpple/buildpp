#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;

    // b->global_flags.compile_driver = "clang++";

    auto foo = b->addSubproject("foo", "my_foo_as_subproject");

    auto main = b->addExe({
        .name = "main", .desc = "My simple binary artefact",
        .obj = {.include_paths = {{.path = foo->b->out / "include"}} },
        .link = {.library_paths = {{.path = foo->b->out / "lib"}}, .libraries_system = {"foo"} }
    }, {"main.cpp"});
    main->dependExeOn(foo->b->install_step);

    b->installExe(main);

    b->addRunExe(main, {.name = "run", .desc = "Run the main executable", .ld_library_paths = {foo->b->out / "lib"}, .args = b->cli_args});
}
