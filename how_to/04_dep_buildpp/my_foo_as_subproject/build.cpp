#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "../buildpp.h"

void configure(Build* b) {
    b->dump_compile_commands = true;

    auto libfoo = b->addLib({
        .name = "foo", .desc = "My simple binary artefact",
        /*.link = StaticLinkTool{"gcc-ar"},*/
    }, {"foo.cpp"});
    b->installLib(libfoo);
    b->installHeaders({"foo.h"}, {.as_tree = true});
}
