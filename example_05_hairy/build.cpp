#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

// you can use your functions here
void genMyFile(Build* b, Path out_path, Path config) {
    Colorizer c{stdout};
    log("%sWriting to %s based on %s%s\n", c.cyan(), out_path.c_str(), config.c_str(), c.reset());
    auto content = "constexpr std::string_view flag = R\"(" + escapeStringJSON(readEntireFile(config)) + ")\";\n";
    log("%sGenerated content:%s\n%s\n", c.green(), c.reset(), content.c_str());
    writeEntireFile(out_path, content);
}

void configure(Build* b) {
    auto enable_x = b->option<bool>("enable-x", "Enable feature X").value_or(false);

    auto gen_includes_path = Path{"generated"} / "include";
    auto common_flags = CXXFlagsOverlay{
        .include_paths = { {.path = b->out / gen_includes_path} }, // here 
        .defines = { {"ENABLE_FEATURE_X", enable_x ? "1" : "0"} },
        .warnings = true,
        .optimize = Optimize::O2,
        .standard = CXXStandard::CXX20,
    };

    auto foobar = b->addLib({
        .name = "foobar", 
        .obj = common_flags,
        .static_lib = true,
    }, {
        "src/foo.cpp",
        "src/bar.cpp",
    });
    b->installLib(foobar);

    auto main = b->addExe({
        .name = "main",
        .desc = "My main binary artefact, that depends on libfoobar",
        .obj = common_flags,
        .link = common_flags,
    }, {
        "src/main.cpp",
    });
    main->link_step->inputs.push_back({.step = foobar->link_step}); // linking
    b->installExe(main);

    auto codegen = b->addStep({
        .name = "codegen",
        .desc = "Generates code based on configuration",
    });
    codegen->inputs.push_back(b->addFile("configs/codegen.txt")); // make cg depend on config file
    auto codegened_path = gen_includes_path / "file.h";
    // you provide the build action for this step
    // inputs are available via b->completedInputs(codegen)
    codegen->inputs_hash = [](Hash h) { return h.combine(hashString("my-codegen-stable-id")); }; // so that hash is not same as input file, but a biection from set of input files to completly different set of codegened files
    codegen->action = [=](Output out) { genMyFile(b, out, b->completedInputs(codegen).at(0)); };

    // install generated file to include path
    auto installed_cg = b->install(codegen, codegened_path);
    // ensure codegen runs and copied to codegened_path before library is built
    foobar->dependLibOn(installed_cg);

    // Creates "Step" that will execute artefact of some target with arguments.
    b->addRunExe(main, { .name = "run", .desc = "Run the main executable", .args = b->cli_args});

    // this will dump compile_commands.json , needed for ides to reason about how your program builds
    b->dump_compile_commands = true;
}
