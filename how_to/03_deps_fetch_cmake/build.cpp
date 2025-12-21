#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

#include <map>

std::pair<Step*, Path> gtestInstall(Build* b, std::string version) {
    std::map<std::string, Hash> googletest_hashes{
        {"1.17.0", Hash{16212965419792237761ULL}},
        {"1.16.0", Hash{0}}, // run it and grab the hash from error message
    };
    auto expected_hash = googletest_hashes[version];
    std::string url = "https://github.com/google/googletest/releases/download/v" + std::string(version) + "/googletest-" + std::string(version) + ".tar.gz";

    auto gtest_tarball = b->fetchByUrl("gtest-tarball", Url{url}, expected_hash);
    auto gtest_sources = b->unpackTar("gtest-sources", gtest_tarball);
    auto gtest = b->runCMake(gtest_sources, "all", {
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DBUILD_GMOCK=OFF",
        "-DBUILD_SHARED_LIBS=OFF",
    });

    auto gtest_prefix = b->out / "deps" / "gtest";
    return {b->install(gtest, gtest_prefix), gtest_prefix};
}

void configure(Build* b) {
    b->dump_compile_commands = true;

    b->global_flags.compile_driver = "clang++";

    auto main = b->addExe({
        .name = "main", .desc = "My simple binary artefact",
        .link = { .libraries_system = {"raylib", "X11", "GL", "m", "pthread", "dl", "rt", "xcb"}}
    }, {"main.cpp"});
    b->installExe(main);
    b->addRunExe(main, {.name = "run", .desc = "Run main exe", .args = b->cli_args});

    auto [gtest, gtest_prefix] = gtestInstall(b, "1.17.0");
    auto unittests = b->addExe({
        .name = "unittests", .desc = "Build unit tests",
        .obj = {
            .include_paths = {{.path = gtest_prefix / "include"}},
        },
        .link = {
            .library_paths = {{.path = gtest_prefix / "lib64"}},
            .libraries_system = {"gtest", "gtest_main"},
        }
    }, {"test.cpp"});
    unittests->dependExeOn(gtest);
    b->installExe(unittests);
    b->addRunExe(unittests, {.name = "test", .desc = "Run unit tests", .ld_library_paths = {gtest_prefix / "lib64"}, .args = b->cli_args});
}
