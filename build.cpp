// This is a bootstrap command, except it's missing "-o b && ./b -h"
#define RECOMPILE_SELF_CMD "clang++ -g -std=c++20 build.cpp"
#include "../buildpp.h"

#include <fstream>

// you can use your functions here
void genMyFile(Build* b, Path out_path, Path gen_config_path) {
    printf("Writing to %s based on %s\n", out_path.c_str(), gen_config_path.c_str());
    std::ifstream in{gen_config_path, std::ios_base::in};
    if (!in.is_open()) b->panic("Can not open file %s", gen_config_path.c_str());
    bool value;
    in >> value;

    std::fstream out{out_path, std::ios_base::out};
    if (!out.is_open()) b->panic("Can not open file %s", out_path.c_str());
    out << "constexpr bool flag =";
    out << value;
    out << ";";
}

void build(Build* b) {
    // this option will automatically pop-up at ./b help message
    // note, that if you remove option, it will still be on the help list
    // to remove it from there, re-bootstrap build
    auto cg_cfg = b->option<std::string>("codegen-configuration", "My very nice option description");

    auto gen_config_path = Path{"gen_config.json"};
    auto gen_includes_path = b->out / "generated" / "include";

    if (cg_cfg) { // example of optional codegen in configuration-time, just if-statement
        genMyFile(b, gen_includes_path / cg_cfg.value(), gen_config_path);
    }

    // creates several "Steps" of compilation
    // you can view list of them using `./b list` (some of them are hidden)
    auto main = b->addTarget({
        .name = "main", .desc = "Main executable",
        .type = Target::Type::Exe,
        // sources and output path is appended to the end as ` -o {out} [{source-file}..]`
        // command must be ready for it
        // in future I want to add some helpers to generate this command based on compiler you are using
        // Big limitation here is the single output file of every "Step" :(
        .command = "clang++ -x c++ -Wall -I " + gen_includes_path.string(),
        .sources = {
            Path{"main.cpp"},
            Path{"foo.cpp"},
            Path{"bar.cpp"},
        },
    });
    // Creates new "Step" (and returns it), that will copy file into output folder, if something changed
    // results of installed "Step"s will be copied into "build" folder
    b->install(main->step, Path{"bin/main"});

    // Example of code-generation during build-time
    // we inject into build graph and add 
    auto cg_build = b->option<std::string>("codegen-build");
    auto build_codegen = b->addStep({
        .name = "generate-file-in-build-time",
        .desc = "Demonstration of codegen in built-time",
        .phony = true, // always out-of-date
    });
    // this hook is crucial. hash you return will be used to access kv-cache node
    // in this example we ignore input hash (of our dependencies) and return hash of our input
    build_codegen->scan_deps = [=](Hash) { return stableHashFile(gen_config_path); };
    // this lambda will be called after build(b) returns at build-time
    // you can access your dependencies and their arts here
    // "out" is the file path you need to fill. it will be stored to be reused between runs
    build_codegen->action = [=](Path out) { genMyFile(b, out, gen_config_path); };

    // example of using result of installation
    auto installed = b->install(build_codegen, Path{"generated/include/file.h"});
    main->step->dependOn(installed);

    // Creates "Step" that will execute art of some target with arguments
    // b->cli_args is arguments you can supply like this:
    // ./b step1 step2 step3 -- arg1 arg2 ...
    // you can append something to them, however you like
    b->addTargetRun(main, { .name = "run", .args = b->cli_args});

    // this will dump compile_commands.json , needed for ides to reason about how your program builds
    // best is to call it at the end of the file
    b->generateCompileCommandsJson(Dir{"."});
}
