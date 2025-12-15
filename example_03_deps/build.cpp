#define BPP_RECOMPILE_SELF_CMD "clang++ -g -O0 -w"
#include "buildpp.h"

#include <map>

std::pair<Step*, Path> gtestInstall(Build* b, std::string version) {
    std::map<std::string, Hash> googletest_hashes{
        {"1.17.0", Hash{12898876601008198337ULL}},
        {"1.16.0", Hash{241731799862153022ULL}},
    };
    auto expected_hash = googletest_hashes[version];
    std::string url = "https://github.com/google/googletest/releases/download/v" + std::string(version) + "/googletest-" + std::string(version) + ".tar.gz";

    auto gtest = b->cmakeFromTarballUrl(
        "gtest",
        Url{url},
        expected_hash,
        {
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DBUILD_GMOCK=OFF",
            "-DBUILD_SHARED_LIBS=ON",
        }
    );
    auto gtest_prefix = b->out / "deps" / "gtest";
    return {b->install(gtest, gtest_prefix), gtest_prefix};
}

void configure(Build* b) {
    b->dump_compile_commands = true;

    auto main = b->addExe({
        .name = "main", .desc = "My simple binary artefact",
        .obj = Flags{
            .compile_driver = "clang++",
            .asan = false,
        },
        .link = Flags{
            .compile_driver = "clang++",
            .libraries_system = {"raylib", "X11", "GL", "m", "pthread", "dl", "rt", "xcb"},
            .asan = false,
        }
    }, {"main.cpp"});
    b->installExe(main, "example_03_deps");
    b->addRun(main, {.name = "run", .desc = "Run main exe", .args = b->cli_args});

    auto [gtest, gtest_prefix] = gtestInstall(b, "1.17.0");
    auto unittests = b->addExe({
        .name = "unittests", .desc = "Build unit tests",
        .obj = Flags{
            .include_paths = {{.path = gtest_prefix / "include"}},
        },
        .link = Flags{
            .library_paths = {{.path = gtest_prefix / "lib64"}},
            .libraries_system = {"gtest", "gtest_main"},
        }
    }, {"test.cpp"});
    unittests->dependExeOn(gtest);
    b->installExe(unittests, "test");
    b->addRun(unittests, {.name = "test", .desc = "Run unit tests", .ld_library_paths = {gtest_prefix / "lib64"}, .args = b->cli_args});
}
