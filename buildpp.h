#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <array>
#include <cstdint>
#include <sstream>

#include <unistd.h>
#include <signal.h> // for raise
#include <dlfcn.h>

#ifndef BPP_RECOMPILE_SELF_CMD
#error R"(To use this library you need to setup how this script will be compiled This is done through this macro (where error is emited). Try to define it just before including header as follows: clang++ -O0)"
#endif

struct Build;
void configure(Build* b); // expected signature of build function

static std::mutex print_mutex;
inline int log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(print_mutex);
    va_list args;
    va_start(args, fmt);
    auto res = vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
    return res;
}

inline int vlog(const char* fmt, va_list args) {
    std::lock_guard<std::mutex> lock(print_mutex);
    auto res = vprintf(fmt, args);
    fflush(stdout);
    return res;
}

struct Hash {
    uint64_t value = 0;

    [[nodiscard]] Hash combine(Hash other) const {
        uint64_t combined = 14695981039346656037ULL;
        combined ^= value;
        combined *= 1099511628211ULL;
        combined ^= other.value;
        combined *= 1099511628211ULL;
        return Hash{combined};
    }

    [[nodiscard]] Hash combineUnordered(Hash other) const {
        return Hash{value + other.value}; // owerflows and it's ok
    }
};

struct Option {
    std::string key;
    std::string description = "";
};

struct Colorizer {
    bool enabled;
    FILE* out;

    Colorizer(FILE* out) : enabled(isatty(fileno(out))), out(out) {}
    [[nodiscard]] const char* red() { return enabled ? "\033[1;31m" : ""; }
    [[nodiscard]] const char* green() { return enabled ? "\033[1;32m" : ""; }
    [[nodiscard]] const char* yellow() { return enabled ? "\033[1;33m" : ""; }
    [[nodiscard]] const char* blue() { return enabled ? "\033[1;34m" : ""; }
    [[nodiscard]] const char* cyan() { return enabled ? "\033[1;36m" : ""; }
    [[nodiscard]] const char* magenta() { return enabled ? "\033[1;35m" : ""; }
    [[nodiscard]] const char* white() { return enabled ? "\033[1;37m" : ""; }
    [[nodiscard]] const char* black() { return enabled ? "\033[1;30m" : ""; }
    [[nodiscard]] const char* gray() { return enabled ? "\033[1;90m" : ""; }
    [[nodiscard]] const char* cyan_bright() { return enabled ? "\033[1;96m" : ""; }
    [[nodiscard]] const char* bold() { return enabled ? "\033[1m" : ""; }
    [[nodiscard]] const char* reset() { return enabled ? "\033[0m" : ""; }
    [[nodiscard]] const char* discard_prev_line() { return enabled ? "\033[1A\033[2K" : ""; }

    int printfFlush(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        int res = vfprintf(out, fmt, args);
        va_end(args);
        fflush(out);
        return res;
    }
};

[[noreturn]] inline void exitFailedOrTrap(int code) {
#ifdef BPP_DEBUG_MODE
    raise(SIGTRAP);
#endif
    exit(code);
}

#define panic(fmt, ...) \
    do { \
        fflush(NULL); \
        Colorizer c{stderr}; \
        log("%sbuildpp:%s %serror: %s%s" fmt "%s", c.gray(), c.reset(), c.red(), c.reset(), c.bold(), ##__VA_ARGS__, c.reset()); \
        exitFailedOrTrap(1); \
    } while (0)

inline std::string escapeStringJSON(std::string_view arg) {
    std::string escaped;
    for (char c : arg) {
        if (c == '"') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

inline std::string escapeStringBash(std::string_view arg) {
    std::string escaped;
    for (char c : arg) {
        if (c == '\'' || c == '"' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

using Path = std::filesystem::path;
using Dir = std::filesystem::path;
using Inputs = std::vector<Path>;
using Output = Path;

struct Step; // forward declare

// if step is defined, path points into step's output
// else path is just a normal path
struct LazyPath {
    Step* step = nullptr;
    Path path = "";
};

struct Step {
    struct Options {
        std::string name;
        std::string desc;
        bool phony{false};
        bool silent{false};
    } opts;

    // Plain dependencies - other steps that must be completed before this one.
    std::vector<Step*> deps = {};
    // Input dependencies - can be used in target command template as {in}
    std::vector<LazyPath> inputs = {};

    std::function<Hash(Hash)> inputs_hash = [](Hash h) { return h; }; // what this step depends other than other steps
    std::function<void(Output)> action = [](Output) {};

    // NOTE: Step is considered up-to-date if its hash + combined hash of dependencies does not exist in cache
    std::optional<Hash> hash;

    // this thread-safety stuff is needed to allow parallel builds not stupidly wait for 16ms on each step
    bool completed = false;
    std::mutex completion_mutex;
    std::condition_variable completion_cv;

    bool threadSafeIsCompleted() {
        std::lock_guard<std::mutex> lock(completion_mutex);
        return completed;
    }

    void markCompleted() {
        std::lock_guard<std::mutex> lock(completion_mutex);
        completed = true;
        completion_cv.notify_all();
    }

    void waitUntilCompleted() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_cv.wait(lock, [this]() { return completed; });
    }

    void dependOn(Step* other) {
        deps.push_back(other);
    }
};

struct Define {
    std::string name;
    std::string value;
};

enum Optimize {
    Default,
    O0,
    O1,
    O2,
    O3,
    Fast,
};

enum class CXXStandard {
    Default,
    CXX11,
    CXX14,
    CXX17,
    CXX20,
    CXX23,
};

// leaving nullopt will make it be taken from global flags
struct CXXFlagsOverlay {
    std::optional<Path> compile_driver;
    std::vector<LazyPath> include_paths = {}; // -I
    std::vector<LazyPath> library_paths = {}; // -L
    std::vector<LazyPath> libraries = {}; // -l:
    std::vector<std::string> libraries_system = {}; // -l
    std::vector<Define> defines = {}; // -D
    std::optional<bool> warnings;
    std::optional<Optimize> optimize;
    std::optional<CXXStandard> standard;
    std::string extra_flags = "";
};

struct CXXFlags {
    Path compile_driver = "g++";
    std::vector<LazyPath> include_paths = {}; // -I
    std::vector<LazyPath> library_paths = {}; // -L
    std::vector<LazyPath> libraries = {}; // -l:
    std::vector<std::string> libraries_system = {}; // -l
    std::vector<Define> defines = {}; // -D
    bool warnings = true;
    Optimize optimize = Optimize::O1;
    CXXStandard standard = CXXStandard::CXX17;
    std::string extra_flags = "";
};

inline CXXFlags applyFlagsOverlay(CXXFlags f1, const CXXFlagsOverlay* f2) {
    if (f2->compile_driver.has_value()) f1.compile_driver = f2->compile_driver.value();
    f1.include_paths.insert(f1.include_paths.end(), f2->include_paths.begin(), f2->include_paths.end());
    f1.library_paths.insert(f1.library_paths.end(), f2->library_paths.begin(), f2->library_paths.end());
    f1.libraries.insert(f1.libraries.end(), f2->libraries.begin(), f2->libraries.end());
    f1.libraries_system.insert(f1.libraries_system.end(), f2->libraries_system.begin(), f2->libraries_system.end());
    f1.defines.insert(f1.defines.end(), f2->defines.begin(), f2->defines.end());
    if (f2->warnings.has_value()) f1.warnings = f2->warnings.value();
    if (f2->optimize.has_value()) f1.optimize = f2->optimize.value();
    if (f2->standard.has_value()) f1.standard = f2->standard.value();
    if (!f2->extra_flags.empty()) {
        if (!f1.extra_flags.empty()) f1.extra_flags += " ";
        f1.extra_flags += f2->extra_flags;
    }

    return f1;
}

// flags that must be enabled for both obj and link steps to work properly
struct LibOrExeCXXFlagsOverlay {
    std::optional<bool> asan;
    std::optional<bool> debug_info;
    std::optional<bool> lto;
};

struct LibOrExeCXXFlags {
    bool asan;
    bool debug_info;
    bool lto;
};

struct ObjOpts {
    CXXFlagsOverlay flags;
    Path source;

    // if part of lib or exe, points to flags, related to whole lib/exe
    // no need for user to provide it, it's filled automatically on addLib/addExe calls
    // but for manual obj creation it may be useful
    // must point to stable location inside of created Lib or Exe structure
    LibOrExeCXXFlagsOverlay* opt_whole = nullptr;
};

struct Obj {
    ObjOpts opts;
    Step* step;
};

struct ExeOpts {
    std::string name;
    std::string desc = "";
    CXXFlagsOverlay obj = {};
    CXXFlagsOverlay link = {};
    LibOrExeCXXFlagsOverlay exe_flags = {};
};

struct Exe {
    ExeOpts opts;
    Step* link_step;

    // executable depends on other step, meaning that other step must be completed before building this exe
    void dependExeOn(Step* other) {
        link_step->deps.push_back(other);
        for (auto in : link_step->inputs) {
            if (in.step) in.step->deps.push_back(other);
        }
    }
};

struct LibraryOpts {
    std::string name;
    std::string desc = "";
    CXXFlagsOverlay obj = {};
    bool static_lib = true;
    LibOrExeCXXFlagsOverlay lib_flags = {};
};

struct Lib {
    LibraryOpts opts;
    Step* link_step;

    // lib depends on other step, meaning that other step must be completed before building this lib
    void dependLibOn(Step* other) {
        link_step->deps.push_back(other);
        for (auto in : link_step->inputs) {
            if (in.step) in.step->deps.push_back(other);
        }
    }

    std::string libName() const {
        if (opts.static_lib) {
            return "lib" + opts.name + ".a";
        } else {
            return "lib" + opts.name + ".so";
        }
    }
};

struct RunOptions {
    std::string name;
    std::string desc = "";
    Path working_dir = Path{"."}; // working directory to run in
    std::vector<Path> ld_library_paths = {}; // additional library paths to set in LD_LIBRARY_PATH
    std::vector<std::string> args = {}; // arguments to pass to the executable
};

struct SubProjOpts {
    std::string name;
    Dir dir;
};

struct SubProj {
    SubProjOpts opts;
    std::unique_ptr<Build> b; // initialized during build setup
    void* configure_handle = nullptr; // handle to "dlopen-ed" library
};

inline bool hasFileInPath(std::string filename) {
    auto* path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string path_env_str{path_env};
    std::istringstream iss{path_env_str};
    std::string token;
    while (std::getline(iss, token, ':')) {
        auto file_path = Path{token} / filename;
        if (std::filesystem::exists(file_path) && std::filesystem::is_regular_file(file_path)) {
            return true;
        }
    }
    return false;
}

inline Hash hashString(std::string_view str) {
    Hash hash{};
    for (char c : str) {
        hash = hash.combine(Hash{static_cast<uint64_t>(c)});
    }
    return hash;
}

// cached inside of one run. mark this function to be optimized even in debug mode
inline Hash hashFile(Path path) {
    static std::mutex hash_mutex;
    static std::unordered_map<Path, Hash> hash_cache;
    {
        std::lock_guard<std::mutex> lock(hash_mutex);
        if (hash_cache.count(path) > 0) return hash_cache[path];
    }

    auto* fin = std::fopen(path.c_str(), "rb");
    if (!fin) panic("Failed to open file %s for hashing: %s\n", path.c_str(), strerror(errno));

    Hash hash{}; // bogus for empty files
    std::array<char, 32 * 1024> buffer;
    size_t buffer_size = 0;

    auto process_buf = [&]() {
        // first hash as uint64_t
        for (size_t i = 0; i < buffer_size / sizeof(uint64_t); ++i) {
            auto batch = reinterpret_cast<uint64_t*>(buffer.data())[i];
            hash = hash.combine(Hash{batch});
        }
        // handle remaining bytes
        for (size_t i = (buffer_size / sizeof(uint64_t)) * sizeof(uint64_t); i < buffer_size; ++i) {
            hash = hash.combine(Hash{static_cast<uint64_t>(buffer[i])});
        }
        buffer_size = 0;
    };

    while (!feof(fin)) {
        buffer_size += std::fread(buffer.data() + buffer_size, 1, buffer.size() - buffer_size, fin);
        if (buffer_size < buffer.size()) continue;
        process_buf();
    }
    if (buffer_size > 0) {
        process_buf();
    }
    std::fclose(fin);

    std::lock_guard<std::mutex> lock(hash_mutex);
    hash_cache[path] = hash;
    return hash;
}

inline Hash hashDirRec(Dir dir) {
    Hash hash{};
    std::vector<Path> entries;
    for (auto entry : std::filesystem::recursive_directory_iterator{dir}) {
        if (entry.is_regular_file()) {
            entries.push_back(entry.path().lexically_relative(dir));
        }
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& rel : entries) {
        auto full_path = dir / rel;
        hash = hash.combineUnordered(hashString(rel.string()).combine(hashFile(full_path)));
    }
    return hash;
}

inline Hash hashAny(Path path) {
    if (std::filesystem::is_directory(path)) {
        return hashDirRec(path);
    } else {
        return hashFile(path); // no filename mixing here
    }
}

// panic on error
static std::string readEntireFile(Path p) {
    std::ifstream fin{p};
    if (!fin.is_open()) panic("Failed to open file %s for reading\n", p.c_str());
    std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    fin.close();
    return content;
}

// panic on error
static void writeEntireFile(Path p, std::string content) {
    std::ofstream fout{p};
    if (!fout.is_open()) panic("Failed to open file %s for writing\n", p.c_str());
    fout << content;
    fout.close();
}

// panic on error
static std::vector<Path> listFiles(Dir d, std::filesystem::directory_options opts = std::filesystem::directory_options::none) try {
    std::vector<Path> res;
    for (auto entry : std::filesystem::recursive_directory_iterator{d, opts}) {
        if (entry.is_regular_file()) res.push_back(entry.path());
    }

    return res;
} catch (const std::exception& e) {
    panic("Listing files in directory %s failed: %s", d.c_str(), e.what());
}

struct HasherOpts {
    std::string stable_id = {};
    std::vector<Dir> dirs = {};
    std::vector<Path> files = {};
    std::vector<std::string> strings = {};
};

inline std::function<Hash(Hash)> inputsHasher(HasherOpts opts) {
    return [opts](Hash h) {
        h = h.combine(hashString(opts.stable_id));
        for (auto dir : opts.dirs) h = h.combine(hashDirRec(dir));
        for (auto file : opts.files) h = h.combine(hashFile(file));
        for (auto str : opts.strings) h = h.combine(hashString(str));
        return h;
    };
};

struct CompileCommandsEntry {
    std::string command;
    Path file;
    Dir dir;
};

using Clock = std::chrono::high_resolution_clock;
using Timestamp = std::chrono::time_point<Clock>;

inline void commandReplacePatternIfExist(std::string* cmd, std::string_view pattern, std::vector<Path> paths) {
    if (auto pos = cmd->find(pattern); pos != std::string::npos) {
        std::string replacement;
        for (const auto& p : paths) {
            replacement += " \"" + escapeStringBash(p.string()) + "\"";
        }
        cmd->replace(pos, pattern.size(), replacement);
    }
}

struct Url {
    std::string value;
};

// main structure build script will operate on
struct Build {
private:
    int saved_argc;
    std::vector<char*> saved_argv;
    std::vector<std::string> requested_steps;
    bool verbose = false;
    bool silent = false;
    bool report_help = false;

    Dir root;
    Dir cache;

    std::unordered_map<std::string, std::optional<std::string>> parsed_options; // order same as old_options
    std::unordered_map<std::string, Option> options; // contains old options as well
    // std::list<TargetInfo> targets;
    std::list<std::pair<RunOptions, Step*>> runs;
    std::list<Step> steps;
    std::list<Obj> objs;
    std::list<Exe> exes;
    std::list<Lib> libs;
    std::list<SubProj> sub_builds;
    std::vector<std::pair<Step*, Path>> install_list;
    std::vector<CompileCommandsEntry> compile_commands_list;

    bool build_phase_started = false; // for asserts
public:
    Step* install_step = nullptr;
    Step* build_all_step = nullptr;
    Dir out;
    std::vector<std::string> cli_args;
    bool dump_compile_commands = false;
    int max_parallel_jobs = -1;

    std::optional<Path> static_link_tool; // if empty, static linking is not supported
    CXXFlags global_flags = {};
    LibOrExeCXXFlags global_lib_exe_flags = {};

    Build(int argc, char** argv, Path env_root, const char* env_cache, const char* env_prefix, CXXFlags global_flags) {
        this->global_flags = global_flags;

        saved_argc = argc;
        saved_argv.resize(argc + 1);
        for (int i = 0; i < argc; ++i) saved_argv[i] = argv[i];
        saved_argv[argc] = nullptr;

        setupDirectories(env_root, env_cache, env_prefix);

        detectStaticLinkTool();

        // printf("buildpp: using root dir: %s\n", root.c_str());
        // printf("buildpp: using cache dir: %s\n", cache.c_str());
        // printf("buildpp: using output dir: %s\n", out.c_str());
        // printf("buildpp: using static link tool: %s\n", static_link_tool.has_value() ? static_link_tool->c_str() : "<none>");
        // printf("buildpp: using global compile driver: %s\n", global_flags.compile_driver.c_str());
    }

    void setupDirectories(Path env_root, const char* env_cache, const char* env_prefix) {
        std::error_code ec;
        root = env_root;
        if (root.empty()) root = std::filesystem::current_path();
        root = std::filesystem::canonical(root);

        cache = Dir{env_cache ? env_cache : ".cache"}; 
        cache = root / cache;
        std::filesystem::create_directories(cache, ec);
        cache = std::filesystem::canonical(cache);

        out = Dir{env_prefix ? env_prefix : "build"};
        out = root / out;
        std::filesystem::create_directories(out, ec);
        out = std::filesystem::canonical(out);

        std::filesystem::create_directories(cache / "arts");
        std::filesystem::remove_all(cache / "tmp", ec); // may fail
        std::filesystem::create_directories(cache / "tmp"); // may not

        // auto-gitignore areas we manage
        writeEntireFile(cache / ".gitignore", "*");
        writeEntireFile(out / ".gitignore", "*");
    }

    void detectStaticLinkTool() {
        if (hasFileInPath("llvm-ar")) {
            static_link_tool = Path{"llvm-ar"};
        } else if (hasFileInPath("ar")) {
            static_link_tool = Path{"ar"};
        }
    }

    void preConfigure() {
        parseOldOptions();
        parseArgs(); // depends on old_options

        // global install step merges together everything this project installs and packs it into one directory
        install_step = addStep({.name = "install", .desc = "Install targets", .phony = true, .silent = true});
        install_step->inputs_hash = inputsHasher({.stable_id = "install-all"});

        build_all_step = addStep({.name = "build", .desc = "Build all targets", .silent = true});

        // push one compile command for self-build
        auto self_path = (root / "build.cpp").string();
        auto cce = CompileCommandsEntry{
            .command = std::string{BPP_RECOMPILE_SELF_CMD} + " " + self_path + " -DBPP_RECOMPILE_SELF_CMD='\"" + escapeStringBash(BPP_RECOMPILE_SELF_CMD) + "\"'",
            .file = root / "build.cpp",
            .dir = root,
        };
        compile_commands_list.push_back(cce);
    }

    void postConfigure() {
        // create compile_commands list at the end of configuration to be more predictable for user
        std::vector<Path> seen_sources;
        for (const auto& obj : objs) {
            // NOTE: In case user compiles same source into multiple objs with different flags, only first one is recorded and emited with no warnings
            if (std::find(seen_sources.begin(), seen_sources.end(), root / obj.opts.source) != seen_sources.end()) continue; // already recorded
            seen_sources.push_back(root / obj.opts.source);

            auto comp_cmd = std::string{};
            cmdRenderCompileObj(&comp_cmd, obj.opts, {obj.opts.source}, {}, "");
            auto cce = CompileCommandsEntry{
                .command = comp_cmd,
                .file = root / obj.opts.source,
                .dir = root,
            };
            compile_commands_list.push_back(cce);
        }
        if (dump_compile_commands) renderAndDumpCompileCommandsJson(root / "compile_commands.json");
    }

    template <typename T>
    std::optional<T> option(std::string key, std::string description = "No description") {
        if (build_phase_started) panic("Cannot add new option \"%s\" after build phase has started\n", key.c_str());
        // check if the same option exists in old options
        if (options.count(key) == 0) { // we need to recompile self with new option
            Colorizer c{stdout};
            blog("buildpp: %sNew option detected%s -D%s :: \"%s\"\n", c.yellow(), c.reset(), key.c_str(), description.c_str());
            // append options to file
            std::ofstream options_file{selfOptionsPath(), std::ios::app};
            if (!options_file.is_open()) panic("Failed to open options file %s for writing\n", selfOptionsPath().c_str());
            options_file << key << " :: " << description << "\n";
            options_file.close();
            // handle runtime-added new options
            options[key] = Option{.key = key, .description = description};
        }

        if (parsed_options.count(key) == 0 || !parsed_options[key].has_value()) {
            return std::nullopt;
        }

        try {
            if constexpr (std::is_same_v<T, bool>) {
                auto val_str = parsed_options[key].value();
                if (val_str == "1" || val_str == "true" || val_str == "yes") {
                    return true;
                } else if (val_str == "0" || val_str == "false" || val_str == "no") {
                    return false;
                } else {
                    panic("Invalid boolean option value for key: \"%s\" value is \"%s\"\n", key.data(), val_str.data());
                }
            }
            if constexpr (std::is_same_v<T, std::string>) {
                return parsed_options[key].value();
            }

            // best-effort parsing for other types using standard streams
            T value;
            std::istringstream iss{parsed_options[key].value()};
            iss >> value;
            return value;
        } catch (...) {
            panic("Failed to parse option value for key: \"%s\" value is \"%s\"\n", key.data(), parsed_options[key].value().data());
        }
    }

    Exe* addExe(ExeOpts opts, std::vector<Path> sources = {}) {
        if (build_phase_started) panic("Cannot add new executable \"%s\" after build phase has started\n", opts.name.c_str());

        auto step = addStep({.name = opts.name, .desc = opts.desc});
        exes.push_back({.opts = opts, .link_step = step});
        auto exe = &exes.back();
        build_all_step->deps.push_back(step);

        for (auto src : sources) {
            auto obj = addObj({.flags = opts.obj, .source = src, .opt_whole = &exe->opts.exe_flags}, true);
            step->inputs.push_back({.step = obj->step});
        }

        step->inputs_hash = [this, exe](Hash h) { return h.combine(hashExeOpts(exe->opts)); };
        step->action = [this, exe](Output out) {
            std::string cmd;
            cmdRenderLinkExe(&cmd, exe->opts, completedInputs(exe->link_step), out);
            if (verbose) log("Linking exe cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Link step command failed with code %d", res);
        };

        return exe;
    }

    Lib* addLib(LibraryOpts opts, std::vector<Path> sources = {}) {
        if (build_phase_started) panic("Cannot add new library \"%s\" after build phase has started\n", opts.name.c_str());
        auto step = addStep({.name = opts.name, .desc = opts.desc});
        build_all_step->deps.push_back(step);
        libs.push_back({.opts = opts, .link_step = step});
        auto lib = &libs.back();
        step->opts.name = lib->libName();

        for (auto src : sources) {
            auto obj = addObj({.flags = opts.obj, .source = src}, true);
            step->inputs.push_back({.step = obj->step});
        }

        step->inputs_hash = [this, lib](Hash h) { return h.combine(hashLibOpts(lib->opts)); };
        step->action = [this, lib](Output out) {
            std::string cmd;
            cmdRenderLinkLib(&cmd, lib->opts, completedInputs(lib->link_step), out);
            if (verbose) log("Linking lib cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Link step command failed with code %d", res);
        };

        return lib;
    }

    // adds file, wrapped as step. to be used later in some step inputs
    LazyPath addFile(Path src) {
        if (build_phase_started) panic("Cannot add new file \"%s\" after build phase has started\n", src.c_str());
        auto file_step = addStep({.name = "file-" + src.string(), .desc = "File " + src.string(), .silent = true});
        src = root / src; // canonicalize after nice name generation
        file_step->inputs_hash = [this, src](Hash) { return hashFile(src); };
        file_step->action = [this, src](Output out) {
            std::error_code ec;
            std::filesystem::copy_file(src, out, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) panic("Failed to copy file %s to %s: %s\n", src.c_str(), out.c_str(), ec.message().c_str());
        };
        return LazyPath{.step = file_step};
    }

    // step must compile one object file from source
    Obj* addObj(ObjOpts opts, bool silent = false) {
        if (build_phase_started) panic("Cannot add new object file \"%s\" after build phase has started\n", opts.source.c_str());
        auto step = addStep({
            .name = Path{opts.source}.replace_extension("o").string(),
            .desc = "Object file for " + Path{opts.source}.filename().string(),
            .silent = silent
        });
        opts.source = root / opts.source; // make source absolute for cases when build script is run from other dir
        build_all_step->deps.push_back(step);

        objs.push_back({opts, step});
        auto obj = &objs.back();

        step->inputs_hash = [this, obj](Hash h) {
            auto inputs = completedInputs(obj->step);
            h = h.combine(hashString(obj->opts.source.string()));
            h = h.combine(hashFile(obj->opts.source));
            h = h.combine(hashObjOpts(obj->opts));
            Hash src_h = buildEntireSourceFileHashCached(obj->opts, obj->opts.source);
            h = h.combine(src_h);
            return h;
        };
        step->action = [this, obj](Output out) mutable { // action is invoked when step must be performed
            std::string cmd;
            auto flags = obj->opts.flags;
            cmdRenderCompileObj(&cmd, obj->opts, {obj->opts.source}, {}, out); // inputs 
            if (verbose) blog("Compile Obj command: %s\n", cmd.data());

            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to build target: %s\n", obj->step->opts.name.c_str());
        };

        return obj;
    }

    Step* addRun(std::string name, std::string desc) {
        auto step = addStep({.name = name, .desc = desc, .phony = true, .silent = false});
        step->inputs_hash = inputsHasher({.stable_id = "Run " + name});
        return step;
    }

    Step* addRunExe(Exe* exe, RunOptions opts) {
        if (build_phase_started) panic("Cannot add new run step \"%s\" after build phase has started\n", opts.name.c_str());
        auto run = addStep({.name = opts.name, .desc = opts.desc, .phony = true, .silent = false});
        runs.push_back({opts, run});
        run->inputs.push_back({.step = exe->link_step});
        run->action = [this, opts, run, exe](Output) {
            // invoke the built executable with args
            auto cmd = std::string{};
            cmd += "pushd " + opts.working_dir.string() + " > /dev/null && ";
            cmd += "export LD_LIBRARY_PATH=";
            for (const auto& p : opts.ld_library_paths) {
                cmd += p.string() + ":";
            }
            cmd += "$LD_LIBRARY_PATH && ";
            if (run->inputs.size() != 1) panic("Run step invoked with %lu inputs instead of 1\n", run->inputs.size());
            cmd += resolveLazyPath(run->inputs[0]).string() + " ";
            for (const auto& arg : opts.args) {
                cmd += arg + " ";
            }
            cmd += "&& popd > /dev/null";
            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to run exe: %s\n", exe->opts.name.c_str());
        };
        return run;
    }

    Step* installExe(Exe* exe) {
        return install(exe->link_step, Path{"bin"} / exe->opts.name);
    }

    Step* installLib(Lib* lib) {
        return install(lib->link_step, Path{"lib"} / lib->libName());
    }

    struct InstallHeaderOpts {
        Path prefix = "";
        bool as_tree = true;
    };

    void installHeaders(std::vector<Path> headers, InstallHeaderOpts opts) {
        for (auto h : headers) {
            using co = std::filesystem::copy_options;
            auto to = out / "include" / opts.prefix / ((opts.as_tree) ? h : h.filename());
            std::filesystem::create_directories(to.parent_path());
            std::filesystem::copy(root / h, to, co::overwrite_existing);
        }
    }

    Step* install(Step* step, Path dst) {
        if (build_phase_started) panic("Cannot add new install step \"%s\" after build phase has started\n", step->opts.name.c_str());
        auto istep = addStep({.name = "install-" + step->opts.name, .desc = "Installs " + step->opts.name, .silent = true});
        dst = out / dst;
        istep->inputs.push_back({.step = step});
        install_step->inputs.push_back({.step = istep});
        istep->inputs_hash = inputsHasher({.stable_id = istep->opts.name, .strings = {dst.string()}});
        istep->action = [=](Output) mutable {
            if (verbose) blog("Installing step %s output to path %s\n", step->opts.name.c_str(), dst.string().c_str());
            std::filesystem::create_directories((out / dst).parent_path());
            using co = std::filesystem::copy_options;
            std::filesystem::copy(completedInputs(istep).at(0), out / dst, co::recursive | co::overwrite_existing);
        };
        return istep;
    }

    Step* addStep(Step::Options opts) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", opts.name.c_str());
        steps.emplace_back();
        steps.back().opts = opts;
        return &steps.back();
    }

    // requires system to have curl+tar
    Step* fetchByUrl(std::string name, Url url, Hash expected_hash) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", name.c_str());
        auto step = addStep({.name = name, .desc = "", .silent = false});
        step->inputs_hash = [this, url, expected_hash](Hash) { return expected_hash; };
        step->action = [=](Output out) {
            // download tarball from url to tmp path
            std::string cmd = "curl --silent -L \"" + url.value + "\" -o \"" + out.string() + "\"";
            if (verbose) blog("Fetching using cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to download tarball %s from %s\n", name.c_str(), url.value.c_str());

            // verify hash of unpacked tarball
            auto actual_hash = hashAny(out);
            if (actual_hash.value != expected_hash.value) {
                log("Expected hash: %llu\n", expected_hash.value);
                log("Actual   hash: %llu\n", actual_hash.value);
                log("Downloaded path: %s\n", out.string().c_str());
                panic("Hash mismatch for fetched content of step %s from url %s: expected %llu but got %llu\n",
                      name.c_str(), url.value.c_str(), expected_hash.value, actual_hash.value);
            }
        };
        return step;
    }

    Step* unpackTar(std::string name, Step* tarball_step) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", name.c_str());
        auto unpack_step = addStep({.name = name, .desc = "Unpack tarball " + tarball_step->opts.name, .silent = false});
        unpack_step->inputs.push_back({.step = tarball_step});
        unpack_step->inputs_hash = inputsHasher({
            .stable_id = "unpack-tar-" + tarball_step->opts.name,
        });
        unpack_step->action = [=](Output out) {
            std::filesystem::create_directories(out);
            auto tarball_path = completedInputs(unpack_step).at(0);
            std::string cmd = "tar -xf \"" + tarball_path.string() + "\" -C \"" + out.string() + "\" --strip-components=1";
            if (verbose) blog("Unpacking tar cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to unpack tarball in step %s\n", tarball_step->opts.name.c_str());
        };
        return unpack_step;
    }

    Step* runCMake(Step* sources, std::string build_target, std::vector<std::string> cmake_args = {}) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", sources->opts.name.c_str());
        auto step = addStep({.name = sources->opts.name + "-cmake", .desc = "CMake run over " + sources->opts.name, .silent = false});
        step->inputs.push_back({.step = sources});
        step->inputs_hash = inputsHasher({.stable_id = "cmake-" + sources->opts.name, .strings = cmake_args});
        step->action = [=](Output out) {
            std::string cmd;
            int res;
            auto src_dir = completedInputs(step).at(0);
            auto build_dir = newTmpPath();
            std::filesystem::create_directories(out);
            std::filesystem::create_directories(build_dir);

            cmd = "cmake -S \"" + src_dir.string() + "\" -B \"" + build_dir.string() + "\"";
            for (auto arg : cmake_args) cmd += " \"" + arg + "\" ";
            if (verbose) blog("CMake configure cmd: %s\n", cmd.c_str());
            res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to configure CMake project %s\n", sources->opts.name.c_str());

            cmd = "cmake --build \"" + build_dir.string() + "\" --target " + build_target + " -j" + std::to_string(max_parallel_jobs);
            if (verbose) blog("CMake build cmd: %s\n", cmd.c_str());
            res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to build CMake project %s\n", sources->opts.name.c_str());

            cmd = "cmake --install \"" + build_dir.string() + "\" --prefix \"" + out.string() + "\"";
            if (verbose) blog("CMake install cmd: %s\n", cmd.c_str());
            res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to install CMake project %s\n", sources->opts.name.c_str());
        };

        return step;
    }

    Step* cmakeFromTarballUrl(std::string name, Url url, Hash expected_hash, std::vector<std::string> cmake_args = {}) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", name.c_str());
        auto fetch_step = fetchByUrl(name + "-fetch", url, expected_hash);
        auto cmake_step = addStep({.name = name + "-cmake", .desc = "CMake configure-build " + name, .silent = false});
        cmake_step->inputs.push_back({.step = fetch_step});
        cmake_step->inputs_hash = inputsHasher({
            .stable_id = "cmake-configure-build-" + name,
            .strings = cmake_args,
        });
        cmake_step->action = [=](Output out) {
            std::filesystem::create_directories(out);
            auto tarball_dir = completedInputs(cmake_step).at(0);
            auto tmp_build_dir = newTmpPath();
            std::string cmd = "cmake -S \"" + tarball_dir.string() + "\" -B \"" + tmp_build_dir.string() + "\" -DCMAKE_INSTALL_PREFIX=\"" + out.string() + "\"";
            for (const auto& arg : cmake_args) {
                cmd += " " + arg;
            }
            if (verbose) blog("CMake configure cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to configure CMake project %s\n", name.c_str());

            // now build it
            cmd = "cmake --build \"" + tmp_build_dir.string() + "\" --target install -j" + std::to_string(max_parallel_jobs);
            if (verbose) blog("CMake build cmd: %s\n", cmd.c_str());
            res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to build CMake project %s\n", name.c_str());
        };
        return cmake_step;
    }

    // will compile build.cpp of subproject and return Build object to operate on it
    SubProj* addSubproject(std::string name, Dir d) {
        if (build_phase_started) panic("Cannot add new step \"%s\" after build phase has started\n", name.c_str());
        d = root / d;
        auto src = d / "build.cpp";
        if (!std::filesystem::exists(src)) panic("Subproject directory %s does not contain build.cpp\n", d.c_str());

        // first, compile and cache buildpp binary for subproject
        auto sub_buildpp_path = cache / "tmp" / ("buildpp-subproj-" + name);
        auto hash = buildEntireSourceFileHashCached({.flags = {.compile_driver = BPP_RECOMPILE_SELF_CMD}}, src);
        if (!cacheEntryExists(hash)) {
            blog("Compiling build script for subproject %s\n", name.c_str());
            // build shared library for buildpp
            std::string cmd;
            cmd += BPP_RECOMPILE_SELF_CMD;
            cmd += " -shared -fPIC";
            cmd += " -o \"" + sub_buildpp_path.string() + "\"";
            cmd += " \"" + src.string() + "\"";
            if (verbose) blog("Subproject buildpp compile cmd: %s\n", cmd.c_str());
            int res = std::system(cmd.c_str());
            if (res != 0) panic("Failed to compile buildpp for subproject %s\n", name.c_str());
            cacheEntryMoveFromTmp(hash, sub_buildpp_path);
        }

        auto lib = cacheEntryGetPath(hash);
        sub_builds.push_back({.opts = {.name = name, .dir = d}});
        auto subproj = &sub_builds.back();
        subproj->b = std::make_unique<Build>(saved_argc, saved_argv.data(), d, cache.c_str(), (out / name).c_str(), global_flags);

        // load library, let it live forever
        subproj->configure_handle = dlopen(lib.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!subproj->configure_handle) panic("Failed to dlopen buildpp subproject library %s: %s\n", lib.c_str(), dlerror());
        using ConfF = void (*)(Build*);
        auto configure_fn = (ConfF)dlsym(subproj->configure_handle, "configure_stable");
        if (!configure_fn) panic("Failed to find symbol \"configure_stable\" in subproject library %s: %s\n", lib.c_str(), dlerror());
        subproj->b->preConfigure();
        configure_fn(subproj->b.get());
        subproj->b->dump_compile_commands = false; // force-disable compile commands dump for subprojects
        subproj->b->postConfigure();
        subproj->b->build_phase_started = true; // prevent further configuration
        for (auto cce : subproj->b->compile_commands_list) compile_commands_list.push_back(cce); // just copy them
        return subproj;
    }

    std::unordered_map<std::string, Exe*> allExes() {
        std::unordered_map<std::string, Exe*> res;
        for (auto& exe : exes) {
            res[exe.opts.name] = &exe;
        }
        return res;
    }

    std::unordered_map<std::string, Lib*> allLibs() {
        std::unordered_map<std::string, Lib*> res;
        for (auto& lib : libs) {
            res[lib.opts.name] = &lib;
        }
        return res;
    }

    // you should not call it on your own
    void runBuild() {
        if (build_phase_started) panic("Build phase already started. Do NOT call runBuildPhase() multiple times\n");
        build_phase_started = true;
        if (report_help) {
            reportHelp();
            return;
        }

        std::vector<Step*> all_steps_flat;
        for (auto& step : steps) all_steps_flat.push_back(&step);
        // perform requested steps in order
        std::vector<Step*> steps_to_perform;
        for (const auto& step_name : requested_steps) {
            bool found = false;
            for (auto& step : steps) {
                if (step.opts.name == step_name) {
                    steps_to_perform.push_back(&step);
                    found = true;
                }
            }
            if (!found) panic("Requested step \"%s\" not found in build script\n", step_name.data());
        }

        std::vector<Step*> steps_run_order; // popped from back
        enum Color {
            White = 0,
            Gray,
            Black,
        };
        std::unordered_map<Step*, Color> step_visited;
        std::vector<Step*> gray_stack;
        std::function<void(Step*)> visit = [&](Step* cur) {
            if (step_visited[cur] == Black) return;
            if (step_visited[cur] == Gray) {
                std::stringstream cycle;
                cycle << "Cyclic dependency in build graph: ";
                cycle << cur->opts.name << " -> ";
                auto it = gray_stack.rbegin();
                while (it != gray_stack.rend()) {
                    cycle << (*it)->opts.name;
                    if ((*it) == cur) break;
                    cycle << " -> ";
                    it++;
                }
                panic("%s\n", cycle.str().c_str());
            }
            step_visited[cur] = Gray;
            gray_stack.push_back(cur);
            for (auto* dep : cur->deps) {
                visit(dep);
            }

            for (auto dep : cur->inputs) {
                if (!dep.step) continue;
                visit(dep.step);
            }
            steps_run_order.push_back(cur);
            step_visited[cur] = Black;
            gray_stack.pop_back();
        };

        for (size_t i = 0; i < steps_to_perform.size(); i++) {
            visit(steps_to_perform[i]);
        }

        for (size_t i = 0; i < steps_run_order.size() / 2; ++i) {
            std::swap(steps_run_order[i], steps_run_order[steps_run_order.size() - i - 1]);
        }

        std::mutex queue_mutex;
        std::vector<std::thread> worker_threads;
        for (size_t i = 0; i < max_parallel_jobs; i++) {
            worker_threads.emplace_back([this, &queue_mutex, &steps_run_order]() {
                try {
                    while (true) {
                        Step* step = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex);
                            if (steps_run_order.empty()) return;
                            step = steps_run_order.back();
                            steps_run_order.pop_back();
                        }
                        for (auto dep : step->deps) {
                            dep->waitUntilCompleted();
                        }
                        for (auto dep : step->inputs) {
                            if (!dep.step) continue;
                            dep.step->waitUntilCompleted();
                        }

                        performStepIfNeeded(step);
                    }
                } catch (const std::exception& e) {
                    panic("Worker thread caught exception: %s\n", e.what());
                }
            });
        }

        for (auto& thread : worker_threads) {
            thread.join();
        }
    }

    std::vector<Path> completedInputs(Step* step) {
        if (!build_phase_started) panic("completedInputs(step) is available only inside of step action that is executed after build phase started\n");
        std::vector<Path> res;
        for (auto input : step->inputs) {
            if (!input.step->threadSafeIsCompleted()) panic("Input step %s of step %s is not completed before dependant\n", input.step->opts.name.c_str(), step->opts.name.c_str());
            res.push_back(resolveLazyPath(input));
        }
        return res;
    }

    Path newTmpPath() {
        static std::random_device rd;
        std::mt19937 rng{rd()};
        std::uniform_int_distribution<uint64_t> dist;
        while (true) {
            auto rand_val = dist(rng);
            auto tmp_path = cache / "tmp" / std::to_string(rand_val);
            if (!std::filesystem::exists(tmp_path)) {
                return tmp_path;
            }
        }
    }

    void recompileBuildScriptIfChanged() {
        auto new_hash = buildEntireSourceFileHashCached({.flags = {.compile_driver = BPP_RECOMPILE_SELF_CMD}}, root / "build.cpp");
        // this if helps avoid rebuilding tool if cache is purged completely
        std::ifstream hash_file{selfHashPath()};
        if (!hash_file.is_open()) recompileSelf(new_hash, "build tool hash file missing, can't verify self-consistency");
        Hash old_hash{0};
        hash_file >> old_hash.value;
        hash_file.close();
        if (old_hash.value != new_hash.value) recompileSelf(new_hash, "source hashes differ");
    }

private:
    void cmdRenderCXXFlags(std::string* cmd, CXXFlagsOverlay flags_overlay) {
        auto flags = applyFlagsOverlay(global_flags, &flags_overlay);
        *cmd += flags.compile_driver;
        *cmd += " " + flags.extra_flags;
        for (const auto& def : flags.defines) {
            *cmd += " -D" + def.name;
            if (!def.value.empty()) {
                *cmd += "=" + def.value;
            }
        }
        if (!flags.warnings) *cmd += " -w";
        switch (flags.optimize) {
            case Optimize::Default: break;
            case Optimize::O0: *cmd += " -O0"; break;
            case Optimize::O1: *cmd += " -O1"; break;
            case Optimize::O2: *cmd += " -O2"; break;
            case Optimize::O3: *cmd += " -O3"; break;
            case Optimize::Fast: *cmd += " -Ofast"; break;
        }
        switch (flags.standard) {
            case CXXStandard::Default: break;
            case CXXStandard::CXX11: *cmd += " -std=c++11"; break;
            case CXXStandard::CXX14: *cmd += " -std=c++14"; break;
            case CXXStandard::CXX17: *cmd += " -std=c++17"; break;
            case CXXStandard::CXX20: *cmd += " -std=c++20"; break;
            case CXXStandard::CXX23: *cmd += " -std=c++23"; break;
        }
        for (const auto& inc : flags.include_paths) *cmd += " -I" + resolveLazyPath(inc).string();
        for (const auto& lib_path : flags.library_paths) *cmd += " -L" + resolveLazyPath(lib_path).string();
    }

    void cmdRenderCXXLibs(std::string* cmd, CXXFlagsOverlay flags_overlay) {
        auto flags = applyFlagsOverlay(global_flags, &flags_overlay);
        for (const auto& lib : flags.libraries) *cmd += " -l:" + resolveLazyPath(lib).string();
        for (const auto& lib : flags.libraries_system) *cmd += " -l" + lib;
    }

    void cmdRenderWholeObjOpts(std::string* cmd, const LibOrExeCXXFlagsOverlay* whole) {
        if (!whole) return;
        if (whole->debug_info.value_or(global_lib_exe_flags.debug_info)) *cmd += " -g";
        if (whole->asan.value_or(global_lib_exe_flags.asan)) *cmd += " -fsanitize=address";
        if (whole->lto.value_or(global_lib_exe_flags.lto)) *cmd += " -flto";
    }

    void cmdRenderCompileObj(std::string* cmd, ObjOpts obj, std::vector<Path> sources, std::vector<Path> inputs, Path out) {
        cmdRenderCXXFlags(cmd, obj.flags);
        cmdRenderWholeObjOpts(cmd, obj.opt_whole);

        *cmd += " -c";
        
        for (auto src : sources) *cmd += " \"" + escapeStringJSON(src.string()) + "\"";
        for (auto in : inputs) *cmd += " \"" + escapeStringJSON(in.string()) + "\"";
        cmdRenderCXXLibs(cmd, obj.flags);
        if (!out.empty()) *cmd += " -o " + out.string();
    }

    void cmdRenderLinkExe(std::string* cmd, ExeOpts exe, std::vector<Path> inputs, Path out) {
        cmdRenderCXXFlags(cmd, exe.link);
        cmdRenderWholeObjOpts(cmd, &exe.exe_flags);

        for (auto in : inputs) *cmd += " \"" + escapeStringJSON(in.string()) + "\"";
        cmdRenderCXXLibs(cmd, exe.link);
        if (!out.empty()) *cmd += " -o " + out.string();
    }

    void cmdRenderLinkLib(std::string* cmd, LibraryOpts lib, std::vector<Path> inputs, Path out) {
        if (lib.static_lib) { // static
            if (!this->static_link_tool) panic("Static linking requested but no static link tool configured in Build object\n");
            *cmd += static_link_tool->string();
            *cmd += " rsc";
            *cmd += " " + out.string();
            for (auto in : inputs) *cmd += " \"" + escapeStringJSON(in.string()) + "\"";
        } else {
            cmdRenderCXXFlags(cmd, lib.obj);
            cmdRenderWholeObjOpts(cmd, &lib.lib_flags);
            *cmd += " -shared";
            for (auto in : inputs) *cmd += " \"" + escapeStringJSON(in.string()) + "\"";
            cmdRenderCXXLibs(cmd, lib.obj);
            if (!out.empty()) *cmd += " -o " + out.string();
        }
    }

    Hash hashCXXFlags(const CXXFlagsOverlay& flags_overlay) {
        auto flags = applyFlagsOverlay(global_flags, &flags_overlay);
        Hash hash{};
        for (const auto& def : flags.defines) {
            hash = hash.combine(hashString(def.name));
            hash = hash.combine(hashString(def.value));
        }
        for (const auto& inc : flags.include_paths) {
            hash = hash.combine(hashString(resolveLazyPath(inc).string()));
        }
        for (const auto& lib_path : flags.library_paths) {
            hash = hash.combine(hashString(resolveLazyPath(lib_path).string()));
        }
        for (const auto& lib : flags.libraries) {
            hash = hash.combine(hashString(resolveLazyPath(lib).string()));
        }
        for (const auto& lib : flags.libraries_system) {
            hash = hash.combine(hashString(lib));
        }
        hash = hash.combine(hashString(flags.extra_flags));
        hash = hash.combine(Hash{static_cast<uint64_t>(flags.optimize)});
        hash = hash.combine(Hash{static_cast<uint64_t>(flags.warnings)});
        hash = hash.combine(Hash{static_cast<uint64_t>(flags.standard)});
        return hash;
    }

    Hash hashWholeObjOpts(const LibOrExeCXXFlagsOverlay* opts) {
        if (!opts) return Hash{0};
        Hash h{};
        h = h.combine(Hash{static_cast<uint64_t>(opts->debug_info.value_or(global_lib_exe_flags.debug_info))});
        h = h.combine(Hash{static_cast<uint64_t>(opts->asan.value_or(global_lib_exe_flags.asan))});
        h = h.combine(Hash{static_cast<uint64_t>(opts->lto.value_or(global_lib_exe_flags.lto))});
        return h;
    }

    Hash hashObjOpts(const ObjOpts& opts) {
        auto h = hashCXXFlags(opts.flags);
        h = h.combine(hashString(opts.source.string()));
        h = h.combine(hashWholeObjOpts(opts.opt_whole));
        return h;
    }

    Hash hashExeOpts(const ExeOpts& opts) {
        auto h = hashCXXFlags(opts.link);
        h = h.combine(hashWholeObjOpts(&opts.exe_flags));
        h = h.combine(hashString(opts.name));
        h = h.combine(hashString(opts.desc));
        return h;
    }

    Hash hashLibOpts(const LibraryOpts& opts) {
        auto h = hashCXXFlags(opts.obj);
        h = h.combine(hashWholeObjOpts(&opts.lib_flags));
        h = h.combine(hashString(opts.name));
        h = h.combine(hashString(opts.desc));
        h = h.combine(Hash{static_cast<uint64_t>(opts.static_lib)});
        return h;
    }

    Path resolveLazyPath(LazyPath lp) {
        if (lp.path.empty() && lp.step == nullptr) panic("LazyPath is not properly initialized\n");
        auto base = lp.step != nullptr ? cacheEntryOfStep(lp.step) : root;
        return lp.path.empty() ? base : base / lp.path;
    }

    void reportHelp() {
        Colorizer c{stdout};
        log("%s%sBuild tool help:%s\n", c.cyan_bright(), c.bold(), c.reset());
        log("Usage: %s [options] [steps] [-- run-args]\n", saved_argv[0]);

        log("%s%sOptions:%s\n", c.cyan(), c.bold(), c.reset());
        log("%s  -h, --help%s               Show this help message\n", c.magenta(), c.reset());
        log("%s  -s, --silent%s             Silent mode, suppress output except errors\n", c.magenta(), c.reset());
        log("%s  -v, --verbose%s            Enable verbose output\n", c.magenta(), c.reset());
        log("%s  -j, --jobs <num>%s         Set maximum parallel jobs (default: number of CPU cores)\n", c.magenta(), c.reset());
        log("%s  --dump-compile-commands%s  Dump compile_commands.json file in root directory\n", c.magenta(), c.reset());
        for (const auto& [_, opt] : options) {
            log("%s  -D%s%s", c.magenta(), opt.key.c_str(), c.reset());
            if (!opt.description.empty()) log(" :: %s", opt.description.c_str());
            log("\n");
        }

        log("%s%sCommands:%s\n", c.cyan(), c.bold(), c.reset());
        for (const auto& [opts, step] : runs) {
            log("%s  %s %s:: Run exe %s\n", c.bold(), opts.name.c_str(), c.reset(), step->inputs[0].step ? step->inputs[0].step->opts.name.c_str() : step->inputs[0].path.c_str());
        }

        // Executables
        if (!exes.empty()) {
            log("%s%sExecutables:%s\n", c.cyan(), c.bold(), c.reset());
            for (const auto& exe : exes) {
                auto info = "(obj: " + std::to_string(exe.link_step->inputs.size()) + ")";
                log("%s  %s%s :: %s %s%s%s\n", c.bold(), exe.opts.name.c_str(), c.reset(), exe.opts.desc.c_str(), c.gray(), info.c_str(), c.reset());
            }
        }

        // libs
        if (!libs.empty()) {
            log("%s%sLibraries:%s\n", c.cyan(), c.bold(), c.reset());
            for (const auto& lib : libs) {
                std::string info = lib.opts.static_lib ? "(static)" : "(shared)";
                info += " (obj: " + std::to_string(lib.link_step->inputs.size()) + ")";
                log("%s  %s%s :: %s %s%s%s\n", c.bold(), lib.opts.name.c_str(), c.reset(), lib.opts.desc.c_str(), c.gray(), info.c_str(), c.reset());
            }
        }
    };

    template <typename F>
    auto recordTime(std::atomic<uint64_t>& total_time_us, F&& f) {
        return [this, &total_time_us, f = std::forward<F>(f)]() mutable {
            auto start = Clock::now();
            auto result = f();
            auto end = Clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_time_us += duration;
            return result;
        };
    }

    struct RecordTimeGuard {
        std::atomic<uint64_t>& total_time_us;
        Timestamp start;

        RecordTimeGuard(std::atomic<uint64_t>& total_time_us) : total_time_us(total_time_us) {
            start = Clock::now();
        }

        ~RecordTimeGuard() {
            auto end = Clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_time_us += duration;
        }
    };

    auto recordTimeGuard(std::atomic<uint64_t>& total_time_us) {
        return RecordTimeGuard{total_time_us};
    }

    void renderAndDumpCompileCommandsJson(Path out) {
        // walk targets, prep json
        std::vector<std::string> cmds;

        for (const auto& cce : compile_commands_list) {
            auto cmd = std::string{};
            cmd += "  {\n";
            cmd += "    \"command\":\"" + escapeStringJSON(cce.command) + "\""; cmd += ",\n";
            cmd += "    \"file\":\"" + escapeStringJSON(cce.file.string()) + "\""; cmd += ",\n";
            cmd += "    \"directory\":\"" + escapeStringJSON(cce.dir.string()) + "\"\n";
            cmd += "  }";
            cmds.push_back(cmd);
        }

        auto res = std::string{};
        res += "[\n";
        for (size_t i = 0; i < cmds.size(); i++) {
            res += cmds[i];
            if (i + 1 != cmds.size()) res += ",\n";
        }
        res += "\n]";

        std::filesystem::create_directories(out.parent_path());
        writeEntireFile(out, res);
    }

    void parseArgs() {
        // baked options
        options["compiler"] = Option{.key = "compiler", .description = "Set C++ compiler to use by default"};
        options["optimize"] = Option{.key = "optimize", .description = "Set optimization level (O* or Fast) (default: compiler default)"};
        options["cxx-standard"] = Option{.key = "cxx-standard", .description = "Set C++ standard (c++XX) (default: compiler default)"};
        options["asan"] = Option{.key = "asan", .description = "Enable AddressSanitizer (default: disabled)"};
        options["debug-info"] = Option{.key = "debug-info", .description = "Generate debug info (default: enabled)"};
        options["lto"] = Option{.key = "lto", .description = "Enable Link Time Optimization (default: disabled)"};

        auto argc = saved_argc;
        auto argv = saved_argv.data();
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--") { // the rest are the args for the run
                cli_args = std::vector<std::string>(argv + i + 1, argv + argc);
                break;
            }

            // options
            if (arg.find("-D", 0) == 0) {
                std::string key = arg.substr(2);
                for (const auto& [opt_key, opt] : options) {
                    if (opt.key + "=" == key.substr(0, opt.key.size() + 1)) { // key=value
                        std::string value = key.substr(opt.key.size() + 1);
                        parsed_options[opt.key] = value;
                        break;
                    }
                    if (opt.key == key) { // key only, treat as boolean true
                        parsed_options[opt.key] = "true";
                        break;
                    }
                }
                continue;
            }

            // builtin options
            if (arg == "-h" || arg == "--help" || arg == "help") {
                report_help = true;
                continue;
            }

            if (arg == "-v" || arg == "--verbose") {
                verbose = true;
                continue;
            }

            if (arg == "-s" || arg == "--silent") {
                silent = true;
                continue;
            }

            if (arg == "-j" || arg == "--jobs") {
                if (i + 1 >= argc) panic("Expected number of jobs after %s\n", arg.data());
                max_parallel_jobs = std::stoi(argv[++i]);
                continue;
            }

            // check if starts with -j or --jobs=
            if (arg.find("-j", 0) == 0 && arg.size() > 2) {
                auto res = std::from_chars(arg.data() + 2, arg.data() + arg.size(), max_parallel_jobs);
                if (res.ec != std::errc()) {
                    panic("Invalid number of jobs in argument \"%s\": %s\n", arg.data(), std::strerror(static_cast<int>(res.ec)));
                }
                continue;
            }

            if (arg == "--dump-compile-commands") {
                dump_compile_commands = true;
                continue;
            }

            // steps to execute sequentially
            requested_steps.push_back(arg);
        }

        auto optimize_opt = option<std::string>("optimize").value_or("default");
        if (optimize_opt == "default") global_flags.optimize = Optimize::Default;
        if (optimize_opt == "O0") global_flags.optimize = Optimize::O0;
        if (optimize_opt == "O1") global_flags.optimize = Optimize::O1;
        if (optimize_opt == "O2") global_flags.optimize = Optimize::O2;
        if (optimize_opt == "O3") global_flags.optimize = Optimize::O3;
        if (optimize_opt == "Fast") global_flags.optimize = Optimize::Fast;

        auto standard_opt = option<std::string>("cxx-standard").value_or("default");
        if (standard_opt == "default") global_flags.standard = CXXStandard::Default;
        if (standard_opt == "c++11") global_flags.standard = CXXStandard::CXX11;
        if (standard_opt == "c++14") global_flags.standard = CXXStandard::CXX14;
        if (standard_opt == "c++17") global_flags.standard = CXXStandard::CXX17;
        if (standard_opt == "c++20") global_flags.standard = CXXStandard::CXX20;
        if (standard_opt == "c++23") global_flags.standard = CXXStandard::CXX23;

        global_lib_exe_flags.asan = option<bool>("asan").value_or(false);
        global_lib_exe_flags.debug_info = option<bool>("debug-info").value_or(true);
        global_lib_exe_flags.lto = option<bool>("lto").value_or(false);

        auto compiler_opt = option<std::string>("compiler");
        if (compiler_opt.has_value()) global_flags.compile_driver = *compiler_opt;

        if (max_parallel_jobs <= 0) max_parallel_jobs = std::thread::hardware_concurrency();
        if (requested_steps.empty()) report_help = true;
    }

    void parseOldOptions() {
        std::ifstream options_file{selfOptionsPath()};
        if (!options_file.is_open()) return; // no old options
        std::string line;
        while (std::getline(options_file, line)) {
            if (line.empty()) continue;
            auto sep_pos = line.find("::");

            // format is: key :: description
            auto key = line.substr(0, sep_pos);
            auto description = sep_pos != std::string::npos ? line.substr(sep_pos + 2) : "";

            // trim spaces
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            description.erase(0, description.find_first_not_of(" \t"));
            description.erase(description.find_last_not_of(" \t") + 1);
            options[key] = Option{.key = key, .description = description};
        }
        options_file.close();
    }

    [[nodiscard]] Hash buildEntireSourceFileHashCached(ObjOpts obj, Path source_file) {
        // scan deps using compiler on source_file
        auto cmd = std::string{};
        cmdRenderCompileObj(&cmd, obj, {source_file}, {}, "{out}");
        cmd += " -M";
        auto inputs_h = hashString(cmd).combine(hashFile(source_file));

        if (!cacheEntryExists(inputs_h)) {
            auto out = newTmpPath();
            commandReplacePatternIfExist(&cmd, "{out}", {out});
            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to scan source file dependencies for file \"%s\"\n    using cmd \"%s\"\n", source_file.c_str(), cmd.c_str());
            cacheEntryMoveFromTmp(inputs_h, out);
        }

        auto deps = parseDepfile(cacheEntryGetPath(inputs_h));
        Hash deps_h{};
        for (auto dep : deps) {
            deps_h = deps_h.combine(hashFile(dep));
        }
        return inputs_h.combine(deps_h);
    }

    void recompileSelf(Hash new_self_hash, const char* reason) {
        Colorizer c{stdout};
        // first things first, save hash
        writeEntireFile(selfHashPath(), std::to_string(new_self_hash.value));
        auto start = Clock::now();
        auto compile = std::string{};
        compile += BPP_RECOMPILE_SELF_CMD;
        compile += " " + (root / "build.cpp").string();
        compile += " -o " + std::string{saved_argv[0]} + " ";
        blog("%s[*] Recompiling build tool, because %s...%s\n", c.yellow(), reason, c.reset());
        auto ret = std::system(compile.data());

        if (ret != 0) {
            // remove hash file to avoid infinite recompilation loop
            std::error_code ec;
            std::filesystem::remove(selfHashPath(), ec);
            panic("Failed to recompile build tool\n");
        }
        // move cursor up one line to overwrite the recompilation message
        auto end = Clock::now();
        blog("%s%s[+] Recompiled build tool in %.2fs%s\n", c.discard_prev_line(), c.gray(), std::chrono::duration<double>(end - start).count(), c.reset());

        // execv to replace current process
        execv(saved_argv[0], saved_argv.data());
        // if execv returns, it failed 
        std::error_code ec;
        std::filesystem::remove(selfHashPath(), ec);
        panic("Failed to exec recompiled build tool\n");
    }

    void performStepIfNeeded(Step* step) {
        if (step->threadSafeIsCompleted()) {
            return;
        }

        // perform dependencies first
        for (auto* dep : step->deps) {
            if (!dep->threadSafeIsCompleted()) panic("Dependency %s of step %s is not completed before dependant\n", dep->opts.name.c_str(), step->opts.name.c_str());
        }

        // perform dependencies first
        for (auto dep : step->inputs) {
            if (dep.step && !dep.step->threadSafeIsCompleted()) panic("Dependency %s of step %s is not completed before dependant\n", dep.step->opts.name.c_str(), step->opts.name.c_str());
        }

        // recalc hash
        Hash h{0};
        for (auto* dep : step->deps) {
            if (!dep->hash.has_value()) panic("Dependency step hash not computed before dependant");
            h = h.combineUnordered(*dep->hash);
        }
        for (auto dep : step->inputs) {
            if (dep.step == nullptr) { continue; }
            if (!dep.step->hash.has_value()) panic("Dependency (input) step hash not computed before dependant");
            h = h.combineUnordered(*dep.step->hash);
        }
        if (step->inputs_hash) {
            h = step->inputs_hash(h);
        }
        step->hash = h;
        auto expected_path = cacheEntryOfStep(step);

        Colorizer c{stdout};

        if (!step->opts.phony) {
            // check if we already have an artifact with the same hash
            if (std::filesystem::exists(expected_path)) {
                if (!step->opts.silent) {
                    if (verbose) blog("%s[step]%s %s%s%s up-to-date!\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset());
                }
                step->markCompleted();
                return;
            } else {
                if (verbose && !step->opts.silent) {
                    blog("%s[step]%s %s%s%s needs to be performed, cache miss at %s\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset(), expected_path.c_str());
                }
            }
        }

        // perform this step
        if (step->action) {
            // if action produces output file, use tmp file and then rename to avoid
            auto tmp_path = newTmpPath();
            step->action(tmp_path);
            if (std::filesystem::exists(tmp_path)) {
                std::error_code ec;
                std::filesystem::rename(tmp_path, expected_path, ec);
                if (ec) panic("Failed to rename tmp file %s to %s: %s\n", tmp_path.c_str(), expected_path.c_str(), ec.message().c_str());
            }
        }

        if (!step->opts.silent) blog("%s[step]%s %s%s%s completed\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset());
        step->markCompleted();
    }

    // entry may be a file or a directory
    Path cacheEntryOfStep(Step* step) {
        auto res = cache / "arts" / std::to_string(step->hash->value);
        std::error_code ec;
        std::filesystem::create_directories(res.parent_path(), ec);
        return res;
    }

    std::vector<Path> parseDepfile(Path depfile) {
        std::ifstream fin{depfile};
        if (!fin.is_open()) panic("Failed to open depfile %s for reading\n", depfile.c_str());
        // skip until ": "
        std::vector<Path> dep_files;
        std::string file;
        char c;
        while (true) {
            c = fin.get();
            if (fin.eof()) break;
            if (c == ':') {
                dep_files.clear();
                file = "";
                continue;
            }
            // handle escaped spaces in filenames
            if (c == ' ' || c == '\n') {
                if (!file.empty()) {
                    dep_files.push_back(Path{file});
                    file = "";
                }
                continue;
            }
            if (c == '\\') {
                c = fin.get();
                if (fin.eof()) break;
                if (file.empty() && (c == ' ' || c == '\n')) {
                    continue; // skip leading spaces
                }
            }
            file += c;
        }
        if (!file.empty()) dep_files.push_back(Path{file});
        return dep_files;
    }

    Path cacheEntryGetPath(Hash h) {
        return cache / "arts" / std::to_string(h.value);
    }

    bool cacheEntryExists(Hash h) {
        auto path = cache / "arts" / std::to_string(h.value);
        return std::filesystem::exists(path);
    }

    void cacheEntryMoveFromTmp(Hash h, Path tmp_path) {
        auto dest_path = cache / "arts" / std::to_string(h.value);
        std::error_code ec;
        std::filesystem::create_directories(dest_path.parent_path(), ec);
        std::filesystem::rename(tmp_path, dest_path, ec);
        if (ec) panic("Failed to move cache entry from tmp %s to %s: %s\n", tmp_path.c_str(), dest_path.c_str(), ec.message().c_str());
    }

    std::filesystem::path selfHashPath() {
        return cache / "bpp.hash";
    }

    std::filesystem::path selfOptionsPath() {
        return cache / "bpp.options";
    }

    // disablable with --silent
    int blog(const char* fmt, ...) {
        if (silent) return 0;
        va_list args;
        va_start(args, fmt);
        auto res = vlog(fmt, args);
        va_end(args);
        return res;
    }
};

inline CXXFlags detectEnvFlags() {
    CXXFlags flags{};
    // detect compiler from environment
    auto env_cxx = std::getenv("CXX");
    if (env_cxx) {
        flags.compile_driver = env_cxx;
    } else {
        flags.compile_driver = "g++";
    }

    // detect CXXFLAGS from environment
    if (auto env_cxxflags = std::getenv("CXXFLAGS"); env_cxxflags) flags.extra_flags = env_cxxflags;

    return flags;
}

extern "C" void configure_stable(Build* b) { // NOLINT
    configure(b);
}

int main(int argc, char** argv) { // NOLINT
    auto env_cache = std::getenv("BPP_CACHE_PREFIX");
    auto env_prefix = std::getenv("BPP_INSTALL_PREFIX");
    Build b{argc, argv, Path{argv[0]}.parent_path().c_str(), env_cache, env_prefix, detectEnvFlags()};
    b.recompileBuildScriptIfChanged();
    b.preConfigure();
    try {
        configure(&b);
    } catch (const std::exception& e) {
        panic("your build script exited with exception: %s\n", e.what());
    }
    b.postConfigure();
    try {
        b.runBuild();
    } catch (const std::exception& e) {
        panic("build phase exited with exception: %s\n", e.what());
    }
    return 0;
}
