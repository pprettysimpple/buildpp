#define BPP_RECOMPILE_SELF_CMD "clang++ -std=c++20 -w -O0 -g"
#include "buildpp.h"

std::pair<Step*, Path> fetchUnpackLuaSources(Build* b, std::string version, Hash expected_hash) {
    auto lua_tarball = b->fetchByUrl("lua-tarball", Url{"https://www.lua.org/ftp/lua-" + version + ".tar.gz"}, expected_hash);
    auto lua_sources = b->unpackTar("lua-sources", lua_tarball);
    auto prefix = b->out / "deps" / "lua-sources";
    auto install = b->install(lua_sources, prefix);
    return {install, prefix};
}

void configure(Build* b) {
    auto [lua_src, lua_prefix] = fetchUnpackLuaSources(b, "5.4.6", Hash{8816149851772971551});
    b->dump_compile_commands = true;

    auto use_readline = b->option<bool>("use-readline", "Use GNU Readline library").value_or(false);

    auto common_defs = std::vector<Define>{};
    auto common_libs = std::vector<std::string>{"m"};

    if (use_readline) {
        common_defs.push_back({"LUA_USE_READLINE", "1"});
        common_libs.push_back("readline");
    }

    auto liblua = b->addLib({.name = "lua", .desc = "Lua interpreter library", .obj = {.defines = common_defs}, .static_lib = true}, {
        lua_prefix / "src/lapi.c",
        lua_prefix / "src/lcorolib.c",
        lua_prefix / "src/ldo.c",
        lua_prefix / "src/linit.c",
        lua_prefix / "src/lmem.c",
        lua_prefix / "src/loslib.c",
        lua_prefix / "src/lstrlib.c",
        lua_prefix / "src/lvm.c",
        lua_prefix / "src/lauxlib.c",
        lua_prefix / "src/lctype.c",
        lua_prefix / "src/ldump.c",
        lua_prefix / "src/liolib.c",
        lua_prefix / "src/loadlib.c",
        lua_prefix / "src/lparser.c",
        lua_prefix / "src/ltable.c",
        lua_prefix / "src/lzio.c",
        lua_prefix / "src/lbaselib.c",
        lua_prefix / "src/ldblib.c",
        lua_prefix / "src/lfunc.c",
        lua_prefix / "src/llex.c",
        lua_prefix / "src/lobject.c",
        lua_prefix / "src/lstate.c",
        lua_prefix / "src/ltablib.c",
        lua_prefix / "src/lundump.c",
        lua_prefix / "src/lcode.c",
        lua_prefix / "src/ldebug.c",
        lua_prefix / "src/lgc.c",
        lua_prefix / "src/lmathlib.c",
        lua_prefix / "src/lopcodes.c",
        lua_prefix / "src/lstring.c",
        lua_prefix / "src/ltm.c",
        lua_prefix / "src/lutf8lib.c",
    });
    liblua->dependLibOn(lua_src);
    
    auto interp = b->addExe({.name = "lua", .desc = "Interpreter utility"}, {lua_prefix / "src/lua.c"});
    interp->dependExeOn(liblua->link_step);
    interp->link_step->inputs.push_back({.step = liblua->link_step}); // link against liblua
    interp->opts.link.libraries_system = common_libs;
    b->installExe(interp);
    b->addRunExe(interp, {.name = "runi", .desc = "Run the main executable", .args = b->cli_args});

    auto compiler = b->addExe({.name = "luac", .desc = "Lua bytecode compiler"}, {lua_prefix / "src/luac.c"});
    compiler->dependExeOn(liblua->link_step);
    compiler->link_step->inputs.push_back({.step = liblua->link_step}); // link against liblua
    compiler->opts.link.libraries_system = common_libs;
    b->installExe(compiler);
    b->addRunExe(compiler, {.name = "runc", .desc = "Run the compiler executable", .args = b->cli_args});
}
