#define BPP_RECOMPILE_SELF_CMD "${CXX} -g"
#include "buildpp.h"

// you can use your functions here
void genMyFile(Build* b, Path out_path, Path gen_config_path) {
    Colorizer c{stdout};
    log("%sWriting to %s based on %s%s\n", c.cyan(), out_path.c_str(), gen_config_path.c_str(), c.reset());
    std::ifstream in{gen_config_path, std::ios_base::in};
    if (!in.is_open()) panic("Can not open file %s", gen_config_path.c_str());
    std::string value;
    in >> value;

    std::filesystem::create_directories(out_path.parent_path());
    std::fstream out{out_path, std::ios_base::out};
    if (!out.is_open()) panic("Can not open file %s", out_path.c_str());
    out << "constexpr std::string_view flag = \"" << escapeStringJSON(value) << "\";";
}

void build(Build* b) {
    auto enable_x = b->option<bool>("enable-x", "Enable feature X");
    auto gen_config_path = b->option<std::string>("codegen-configuration", "My very nice option description").value_or("gen_config.json");
    auto gen_includes_path = b->out / "generated" / "include";
    std::filesystem::create_directories(gen_includes_path);

    auto flags = std::string{};
    flags += " -std=c++20";
    flags += " -g";
    flags += " -fsanitize=address";
    flags += " -flto";

    auto main = b->addExecutable({
        .name = "main",
        .desc = "My main binary artefact",
        .compiler = "${CXX}", // used for both compiling and linking
        .flags = {
            .include_paths = { gen_includes_path },
            .libraries = {},
            .defines = { {"ENABLE_FEATURE_X", enable_x.value_or(false) ? "1" : "0"} },
            .extra_flags = flags, // this options will be passed to both "compiler" and "linker" steps
        },
        .linker_flags = "-gz=zlib", // passed to only link-step
    }, {
        Path{"main.cpp"},
        Path{"foo.cpp"},
        Path{"bar.cpp"},
    });
    b->install(main, "bin/main");

    auto codegen = b->addStep({
        .name = "codegen",
        .desc = "Generates code based on configuration",
        .silent = false,
    });
    auto codegened_path = gen_includes_path / "file.h";
    codegen->inputs_hash = [=](Hash h) -> Hash { return hashFile(gen_config_path); }; // hash everything your step depends on
    codegen->action = [=](Output out) { genMyFile(b, out, gen_config_path); }; // write to out path
    auto installed_cg = b->install(codegen, codegened_path);
    main->dependOn(installed_cg); // ensure codegen runs and copied to codegened_path before main is built

    // Creates "Step" that will execute artefact of some target with arguments.
    b->addRun(main, { .name = "run", .desc = "Run the main executable", .args = b->cli_args});

    // this will dump compile_commands.json , needed for ides to reason about how your program builds
    b->dump_compile_commands = true;
}
