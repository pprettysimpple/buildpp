#pragma once

// header that build.cpp includes
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include <fstream>
#include <array>
#include <cstdint>
#include <sstream>

#include <unistd.h>
#include <csignal>

#ifndef BPP_RECOMPILE_SELF_CMD
#error R"(To use this library you need to setup how this script will be compiled This is done through this macro (where error is emited). Try to define it just before including header as follows: clang++ -g -std=c++20 build.cpp)"
#endif

std::mutex print_mutex;
int log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(print_mutex);
    va_list args;
    va_start(args, fmt);
    auto res = vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
    return res;
}

int vlog(const char* fmt, va_list args) {
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

[[noreturn]] void exitFailedOrTrap(int code) {
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


using Path = std::filesystem::path;
using Dir = std::filesystem::path;
using Inputs = std::vector<Path>;
using Output = Path;

struct Step {
    struct Options {
        std::string name;
        std::string desc;
        bool phony{false};
        bool silent{false};
        bool report_time{false};
    } opts;

    // Plain dependencies - other steps that must be completed before this one.
    std::vector<Step*> deps = {};
    // Input dependencies - can be used in target command template as {in}
    std::vector<Step*> inputs = {};
    // NOTE: For targets, dependency is a bit tricky
    //       If you make executable and make it depend on codegen,
    //       you expect that codegen is run before compiling any target of this exe
    //       But targets are different steps. So if you make target depend on something,
    //       We make each node in target_deps depend on that something
    std::vector<Step*> target_deps = {};

    std::function<Hash(Hash)> inputs_hash = [](Hash h) { return h; }; // what this step depends other than other steps
    std::function<void(Output)> action = [](Output) {};
    size_t idx;

    // NOTE: Step is considered up-to-date if its hash + combined hash of dependencies does not exist in cache
    std::optional<Hash> hash;
    std::unique_ptr<std::atomic<bool>> completed_impl = std::make_unique<std::atomic<bool>>(false); // damn atomic makes it impossible to copy step

    std::atomic<bool>& completed() { // to avoid writing buggy if(step->completed)
        return *completed_impl;
    }

    void dependOn(Step* other) {
        this->deps.push_back(other);
        for (auto* target_dep : this->target_deps) {
            target_dep->dependOn(other);
        }
    }
};

enum class Compiler {
    Clang,
    LDD,
};

struct Define {
    std::string name;
    std::string value;
};

// struct Target {
//     enum class Type {
//         Exe,
//         StaticLib,
//         SharedLib,
//     };

    
// };

struct CompileFlags {
    std::vector<Path> include_paths = {};
    std::vector<Path> libraries = {};
    std::vector<Define> defines = {};
    std::string extra_flags = "";
};

struct Compile {
    std::string name;
    std::string desc = "";

    // Template options:
    // - {src} - source file[s]
    // - {out} - output file (only one)
    std::string cmd;
    CompileFlags flags;

    // if you provide sources, they will be used in compile string as {src}. inputs are still accessible as {in}
    std::vector<Path> sources = {};
};

struct Executable {
    std::string name;
    std::string desc = "";
    Path compiler = "clang++";
    CompileFlags flags;
    std::string linker_flags = "";
};

void commandAddFlags(std::string* out, CompileFlags flags) {
    *out += " " + flags.extra_flags;
    for (const auto& def : flags.defines) {
        *out += " -D" + def.name;
        if (!def.value.empty()) {
            *out += "=" + def.value;
        }
    }
    for (const auto& inc : flags.include_paths) *out += " -I" + inc.string();
    for (const auto& lib : flags.libraries) *out += " -l" + lib.string();
}

struct TargetInfo {
    std::string name;
    std::string desc = "";
    std::variant<Compile> opts;
    Step* step;
};

struct RunOptions {
    std::string name;
    std::string desc = "";
    Path working_dir = Path{"."}; // working directory to run in
    std::vector<std::string> args = {}; // arguments to pass to the executable
};

// cached inside of one run
Hash hashFile(Path path) {
    static std::mutex hash_mutex;
    static std::unordered_map<Path, Hash> hash_cache;
    {
        std::lock_guard<std::mutex> lock(hash_mutex);
        if (hash_cache.count(path) > 0) return hash_cache[path];
    }

    auto* fin = std::fopen(path.c_str(), "rb");
    if (!fin) panic("Failed to open file %s for hashing: %s\n", path.c_str(), strerror(errno));

    Hash hash{666}; // bogus for empty files
    std::array<char, 32 * 1024> buffer;
    while (true) {
        auto sz = std::fread(buffer.data(), 1, buffer.size(), fin);
        if (sz == 0) break;
        // first hash as uint64_t
        for (size_t i = 0; i < sz / sizeof(uint64_t); ++i) {
            auto batch = reinterpret_cast<uint64_t*>(buffer.data())[i];
            hash = hash.combine(Hash{batch});
        }
        // handle remaining bytes
        for (size_t i = (sz / sizeof(uint64_t)) * sizeof(uint64_t); i < sz; ++i) {
            hash = hash.combine(Hash{static_cast<uint64_t>(buffer[i])});
        }
    }
    std::fclose(fin);

    std::lock_guard<std::mutex> lock(hash_mutex);
    hash_cache[path] = hash;
    return hash;
}

inline Hash hashString(std::string_view str) {
    Hash hash{};
    for (char c : str) {
        hash = hash.combine(Hash{static_cast<uint64_t>(c)});
    }
    return hash;
}

inline Hash hashFlags(const CompileFlags& flags) {
    Hash hash{};
    for (const auto& def : flags.defines) {
        hash = hash.combine(hashString(def.name));
        hash = hash.combine(hashString(def.value));
    }
    for (const auto& inc : flags.include_paths) {
        hash = hash.combine(hashString(inc.string()));
    }
    for (const auto& lib : flags.libraries) {
        hash = hash.combine(hashString(lib.string()));
    }
    hash = hash.combine(hashString(flags.extra_flags));
    return hash;
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

struct CompileCommandsEntry {
    std::string command;
    Path file;
    Dir dir;
};

using Clock = std::chrono::high_resolution_clock;
using Timestamp = std::chrono::time_point<Clock>;

std::string escapeStringJSON(std::string_view arg) {
    std::string escaped;
    for (char c : arg) {
        if (c == '"') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

std::string escapeStringBash(std::string_view arg) {
    std::string escaped;
    for (char c : arg) {
        if (c == '\'' || c == '"' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

void commandReplacePatternIfExist(std::string* cmd, std::string_view pattern, std::vector<Path> paths) {
    if (auto pos = cmd->find(pattern); pos != std::string::npos) {
        std::string replacement;
        for (const auto& p : paths) {
            replacement += " \"" + escapeStringBash(p.string()) + "\"";
        }
        cmd->replace(pos, pattern.size(), replacement);
    }
}

// main structure build script will operate on
struct Build {
private:
    int saved_argc;
    std::vector<char*> saved_argv;
    std::vector<std::string> requested_steps;
    bool verbose = false;
    bool silent = false;
    int max_parallel_jobs = -1;

    Dir root;
    Dir cache;

    std::unordered_map<std::string, std::optional<std::string>> parsed_options; // order same as old_options
    std::unordered_map<std::string, Option> options; // contains old options as well
    std::list<TargetInfo> targets;
    std::list<std::pair<RunOptions, Step*>> runs;
    std::list<Step> steps;
    std::vector<std::pair<Step*, Path>> install_list;
    Step* install_step = nullptr;
    Step* build_all_step = nullptr;
    std::vector<CompileCommandsEntry> compile_commands_list;

    Timestamp glob_start_time;
    Timestamp build_phase_start;
public:
    Dir out;
    std::vector<std::string> cli_args;
    std::optional<bool> dump_compile_commands;

    Build(int argc, char** argv, Dir root, Dir cache, Dir out) : root(root), cache(cache), out(out) {
        glob_start_time = Clock::now();
        std::filesystem::create_directories(cache / "arts");
        std::filesystem::create_directories(cache / "deps");

        saved_argc = argc;
        saved_argv.resize(argc + 1);
        for (int i = 0; i < argc; ++i) saved_argv[i] = argv[i];
        saved_argv[argc] = nullptr;

        checkIfBuildScriptChanged();
        parseOldOptions();
        parseArgs(); // depends on old_options

        install_step = addStep({.name = "install", .desc = "Install targets", .silent = false});
        
        build_all_step = addStep({.name = "build", .desc = "Build all targets", .silent = false, .report_time = true});
        build_all_step->dependOn(install_step);
        
        auto list = addStep({.name = "list", .desc = "List available steps", .silent = false});
        list->action = [this](Output) {
            Colorizer c{stdout};

            if (verbose) {
                log("%s%sSteps:%s\n", c.cyan(), c.bold(), c.reset());
                for (const auto& step : steps) {
                    if (step.opts.silent) continue;
                    log("%s%s  %s%s :: %s\n", c.bold(), c.blue(), step.opts.name.c_str(), c.reset(), step.opts.desc.c_str());
                }
            }

            log("%s%sTargets:%s\n", c.cyan(), c.bold(), c.reset());
            for (const auto& ti : targets) {
                if (ti.step->opts.silent) continue;
                log("%s%s  %s%s :: %s\n", c.bold(), c.blue(), ti.name.c_str(), c.reset(), ti.desc.c_str());
            }
        };

        auto help = addStep({.name = "help", .desc = "Show help message", .phony = true, .silent = false});
        help->action = [this](Output) {
            Colorizer c{stdout};
            log("%s%sBuild tool help:%s\n", c.cyan_bright(), c.bold(), c.reset());
            log("Usage: %s [options] [steps] [-- run-args]\n", saved_argv[0]);

            log("%s%sGeneric options:%s\n", c.cyan(), c.bold(), c.reset());
            log("%s  -h, --help%s               Show this help message\n", c.magenta(), c.reset());
            log("%s  -s, --silent%s             Silent mode, suppress output except errors\n", c.magenta(), c.reset());
            log("%s  -v, --verbose%s            Enable verbose output\n", c.magenta(), c.reset());
            log("%s  -j, --jobs <num>%s         Set maximum parallel jobs (default: number of CPU cores)\n", c.magenta(), c.reset());
            log("%s  --dump-compile-commands%s  Dump compile_commands.json file in root directory\n", c.magenta(), c.reset());

            log("%s%sOptions:%s\n", c.cyan(), c.bold(), c.reset());
            for (const auto& [_, opt] : options) {
                log("%s  -D%s%s", c.magenta(), opt.key.c_str(), c.reset());
                if (!opt.description.empty()) {
                    log(" :: %s", opt.description.c_str());
                }
                log("\n");
            }

            log("%s%sCommands:%s\n", c.cyan(), c.bold(), c.reset());
            for (const auto& [opts, step] : runs) {
                log("%s  %s %s:: Run exe %s\n", c.bold(), opts.name.c_str(), c.reset(), step->inputs[0]->opts.name.c_str());
            }
        };
    }

    template <typename T>
    std::optional<T> option(std::string key, std::string description = "No description") {
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

    Step* addExecutable(Executable exe, std::vector<Path> sources = {}) {
        auto link_cmd = std::string{};
        link_cmd += exe.compiler.string();
        commandAddFlags(&link_cmd, exe.flags);
        link_cmd += " -o {out} {in}";
        link_cmd += " " + exe.linker_flags;
        for (auto lib : exe.flags.libraries) {
            link_cmd += " -l" + lib.string();
        }

        auto main = addTarget({
            .name = exe.name,
            .desc = exe.desc,
            .cmd = link_cmd,
            .flags = exe.flags,
            .sources = {}, // link target has no sources
        });
        for (auto src : sources) {
            auto compile_cmd = std::string{};
            compile_cmd += exe.compiler.string();
            commandAddFlags(&compile_cmd, exe.flags);
            compile_cmd += " -o {out} -c {src}";

            auto obj = addTarget({
                .name = Path{src}.replace_extension("o"),
                .cmd = compile_cmd,
                .flags = exe.flags,
                .sources = {src},
            });
            obj->opts.silent = true;
            obj->inputs.push_back(addFile(src));
            main->inputs.push_back(obj);
            main->target_deps.push_back(obj); // ensure main dependencies propagated to obj as well

            // add all object files to compile commands
            auto compile_cmd_full = compile_cmd;
            commandReplacePatternIfExist(&compile_cmd_full, "{in}", {src});
            commandReplacePatternIfExist(&compile_cmd_full, "{out}", {Path{src}.replace_extension("o")});
            compile_commands_list.push_back(CompileCommandsEntry{
                .command = compile_cmd_full,
                .file = src,
                .dir = Path{"."},
            });
        }
        return main;
    }

    // adds file, wrapped as step. to be used later in some step inputs
    Step* addFile(Path src) {
        auto file_step = addStep(Step::Options{.name = "file-" + src.string(), .desc = "File " + src.string(), .silent = true});
        file_step->inputs_hash = [this, src](Hash) { return hashFile(src); };
        file_step->action = [this, src](Output out) {
            std::error_code ec;
            std::filesystem::copy_file(src, out, std::filesystem::copy_options::overwrite_existing, ec);
            if (verbose) blog("Copied file %s to cache: %s\n", src.c_str(), ec.message().c_str());
            if (ec) panic("Failed to copy file %s to %s: %s\n", src.c_str(), out.c_str(), ec.message().c_str());
        };
        return file_step;
    }

    Step* addTarget(Compile opts) {
        auto target = addStep(Step::Options{.name = opts.name, .desc = opts.desc});
        targets.push_back({.name = opts.name, .desc = opts.desc, .opts = opts, .step = target});
        build_all_step->dependOn(target);

        target->inputs_hash = [this, opts, target](Hash h) {
            auto inputs = completedInputs(target);
            h = h.combine(hashString(opts.name));
            h = h.combine(hashFlags(opts.flags));
            h = h.combine(hashString(opts.cmd));
            Hash in_h{0};
            for (auto in : inputs) {
                in_h = in_h.combineUnordered(hashFile(in));
            }
            h = h.combine(in_h);
            Hash src_h{};
            for (auto src : opts.sources) {
                src_h = src_h.combineUnordered(buildEntireSourceFileHashCached(opts.cmd, src, inputs));
            }
            h = h.combine(src_h);
            return h;
        };

        target->action = [this, opts, target](Output out) mutable { // action is invoked when step must be performed
            auto cmd = opts.cmd;
            commandReplacePatternIfExist(&cmd, "{in}", completedInputs(target));
            commandReplacePatternIfExist(&cmd, "{src}", opts.sources);
            commandReplacePatternIfExist(&cmd, "{out}", {out});
            if (verbose) blog("Compile command: %s\n", cmd.data());

            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to build target: %s\n", opts.name.c_str());
        };
        return target;
    }

    Step* addRun(Step* target, RunOptions opts) {
        auto run = addStep({
            .name = opts.name,
            .desc = opts.desc,
            .phony = true,
            .silent = false,
        });
        runs.push_back({opts, run});
        run->inputs.push_back(target);
        run->action = [this, opts, run, target](Output) {
            // invoke the built executable with args
            auto cmd = std::string{};
            cmd += "pushd " + opts.working_dir.string() + " > /dev/null && ";
            if (run->inputs.size() != 1) panic("Run step invoked with %lu inputs instead of 1\n", run->inputs.size());
            cmd += cacheEntryOfStep(run->inputs[0]).string() + " ";
            for (const auto& arg : opts.args) {
                cmd += arg + " ";
            }
            cmd += "&& popd > /dev/null";
            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to run target: %s\n", target->opts.name.c_str());
        };
        return run;
    }

    Step* install(Step* step, Path dest) {
        auto file_install_step = addStep(Step::Options{.name = "install-" + step->opts.name, .desc = "Installs " + step->opts.name, .silent = false});
        file_install_step->inputs.push_back(step);
        install_step->dependOn(file_install_step);
        file_install_step->inputs_hash = [this, step, dest](Hash h) {
            auto child_path = cacheEntryOfStep(step);
            // check if destination file exists and equals to target artifact
            if (!std::filesystem::exists(out / dest)) return Hash{step->hash->value + 1}; // bogus
            auto dest_hash = hashFile(out / dest);
            auto src_hash = hashFile(child_path);
            return dest_hash.value == src_hash.value ? h : Hash{step->hash->value + 1}; // bogus
        };
        file_install_step->action = [this, step, dest, file_install_step](Output) {
            if (file_install_step->inputs.size() != 1) panic("Install step invoked with %lu files instead of 1\n", file_install_step->inputs.size());
            blog("[%s] target %s -> %s\n", file_install_step->opts.name.c_str(), step->opts.name.c_str(), dest.c_str());
            // copy file from artifact cache to dest.path
            // but first ensure parent directory exists
            std::filesystem::create_directories((out / dest).parent_path());
            std::filesystem::copy_file(cacheEntryOfStep(file_install_step->inputs[0]),
                                    out / dest,
                                    std::filesystem::copy_options::overwrite_existing);
        };
        return file_install_step;
    }

    Step* addStep(Step::Options opts) {
        steps.push_back(Step{ .opts = opts, .idx = steps.size() });
        return &steps.back();
    }

    // you should not call it on your own
    void runBuildPhase() {
        if (dump_compile_commands) writeEntireFile(root / "compile_commands.json", generateCompileCommandsJson());

        std::vector<Step*> all_steps_flat;
        for (auto& step : steps) all_steps_flat.push_back(&step);
        // perform requested steps in order
        std::vector<size_t> steps_to_perform;
        for (const auto& step_name : requested_steps) {
            bool found = false;
            for (auto& step : steps) {
                if (step.opts.name == step_name) {
                    steps_to_perform.push_back(step.idx);
                    found = true;
                    break;
                }
            }
            if (!found) panic("Requested step \"%s\" not found in build script\n", step_name.data());
        }

        std::vector<Step*> steps_run_order; // popped from back
        enum Color {
            White,
            Gray,
            Black,
        };
        std::vector<Color> step_visited(steps.size(), White);
        std::vector<Step*> gray_stack;
        std::function<void(size_t)> visit = [&](size_t pos) {
            if (step_visited[pos] == Black) return;
            if (step_visited[pos] == Gray) {
                std::stringstream cycle;
                cycle << "Cyclic dependency in build graph: ";
                cycle << all_steps_flat[pos]->opts.name << " -> ";
                auto it = gray_stack.rbegin();
                while (it != gray_stack.rend()) {
                    cycle << (*it)->opts.name;
                    if ((*it)->idx == pos) break;
                    cycle << " -> ";
                    it++;
                }
                panic("%s\n", cycle.str().c_str());
            }
            step_visited[pos] = Gray;
            gray_stack.push_back(all_steps_flat[pos]);
            steps_run_order.push_back(all_steps_flat[pos]);
            for (auto* dep : all_steps_flat[pos]->deps) {
                visit(dep->idx);
            }

            for (auto* dep : all_steps_flat[pos]->inputs) {
                visit(dep->idx);
            }
            step_visited[pos] = Black;
            gray_stack.pop_back();
        };

        for (size_t i = 0; i < steps_to_perform.size(); i++) {
            visit(steps_to_perform[steps_to_perform.size() - i - 1]);
        }

        build_phase_start = Clock::now();

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
                            while (!dep->completed()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // TODO: Condvar
                            }
                        }
                        for (auto dep : step->inputs) {
                            while (!dep->completed()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // TODO: Condvar
                            }
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

        auto build_phase_end = Clock::now();

        auto configure_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_phase_start - glob_start_time).count();
        auto build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_phase_end - build_phase_start).count();
        if (verbose) {
            Colorizer c{stdout};
            blog("Build took %s%.2fs%s\n", c.yellow(), (configure_time + build_time) / 1000.0, c.reset());
        }
    }

private:
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

    std::string generateCompileCommandsJson() {
        // walk targets, prep json
        std::vector<std::string> cmds;

        auto build_self_cmd = std::string{};
        build_self_cmd += "  {\n";
        build_self_cmd += "    \"command\":\"" BPP_RECOMPILE_SELF_CMD " build.cpp\""; build_self_cmd += ",\n";
        build_self_cmd += "    \"file\":\"build.cpp\""; build_self_cmd += ",\n";
        build_self_cmd += "    \"directory\":\".\"\n";
        build_self_cmd += "  }";
        cmds.push_back(build_self_cmd);

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
        return res;
    }

    void parseArgs() {
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
                requested_steps.push_back("help");
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
                if (i + 1 >= argc) {
                    panic("Expected number of jobs after %s\n", arg.data());
                }
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

        if (max_parallel_jobs <= 0) max_parallel_jobs = std::thread::hardware_concurrency();

        if (requested_steps.empty()) {
            requested_steps.push_back("build");
        }
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

    Hash calcSelfHash() {
        auto start = Clock::now();
        auto cmd = std::string{};
        cmd += BPP_RECOMPILE_SELF_CMD;
        cmd += " -o {out} {src}";
        return buildEntireSourceFileHashCached(cmd, "build.cpp", {});
    }

    [[nodiscard]] Hash buildEntireSourceFileHashCached(std::string compile_cmd, Path source_file, std::vector<Path> in) {
        // scan deps using compiler on source_file
        auto cmd = std::string{};
        cmd += compile_cmd;
        cmd += " -M";
        commandReplacePatternIfExist(&cmd, "{in}", in);
        commandReplacePatternIfExist(&cmd, "{src}", {source_file});

        auto inputs_h = hashString(cmd).combine(hashFile(source_file));
        auto out = cacheEntryOfSourceDepfile(inputs_h);
        commandReplacePatternIfExist(&cmd, "{out}", {out});
        if (!std::filesystem::exists(out)) {
            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to scan source file dependencies for file \"%s\"\n    using cmd \"%s\"\n", source_file.c_str(), cmd.c_str());
        }

        auto deps = parseDepfile(out);
        Hash deps_h{};
        for (auto dep : deps) {
            deps_h = deps_h.combine(hashFile(dep));
        }
        return inputs_h.combine(deps_h);
    }

    void checkIfBuildScriptChanged() {
        auto new_hash = calcSelfHash();
        std::ifstream hash_file{selfHashPath()};
        if (!hash_file.is_open()) recompileSelf(new_hash, "no hash file found");
        Hash old_hash{0};
        hash_file >> old_hash.value;
        hash_file.close();
        if (old_hash.value != new_hash.value) recompileSelf(new_hash, "source hashes mismatch");
    }

    void recompileSelf(Hash new_self_hash, std::string reason) {
        Colorizer c{stdout};
        // first things first, save hash
        writeEntireFile(selfHashPath(), std::to_string(new_self_hash.value));
        auto start = Clock::now();
        auto compile = std::string{};
        compile += BPP_RECOMPILE_SELF_CMD;
        compile += " build.cpp";
        compile += " -o " + std::string{saved_argv[0]} + " ";
        blog("%s[*] Recompiling build tool, because %s...%s\n", c.yellow(), reason.data(), c.reset());
        auto ret = std::system(compile.data());

        if (ret != 0) {
            // remove hash file to avoid infinite recompilation loop
            std::error_code ec;
            std::filesystem::remove(selfHashPath(), ec);
            panic("Failed to recompile build tool\n");
        }
        // move cursor up one line to overwrite the recompilation message
        auto end = Clock::now();
        blog("%s%s[+] Recompiled build tool in %.2f s%s\n", c.discard_prev_line(), c.gray(), std::chrono::duration<double>(end - start).count(), c.reset());

        // execv to replace current process
        execv(saved_argv[0], saved_argv.data());
        // if execv returns, it failed 
        std::error_code ec;
        std::filesystem::remove(selfHashPath(), ec);
        panic("Failed to exec recompiled build tool\n");
    }

    void performStepIfNeeded(Step* step) {
        if (step->completed()) {
            return;
        }

        // perform dependencies first
        for (auto* dep : step->deps) {
            if (!dep->completed()) panic("Dependency %s of step %s is not completed before dependant\n", dep->opts.name.c_str(), step->opts.name.c_str());
        }

        // recalc hash
        Hash h{0};
        for (auto* dep : step->deps) {
            if (!dep->hash.has_value()) panic("Dependency step hash not computed before dependant");
            h = h.combineUnordered(*dep->hash);
        }
        for (auto* dep : step->inputs) {
            if (!dep->hash.has_value()) panic("Dependency (input) step hash not computed before dependant");
            h = h.combineUnordered(*dep->hash);
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
                    blog("%s[step]%s %s%s%s up-to-date!\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset());
                }
                step->completed() = true;
                return;
            }
        }

        // perform this step
        if (step->action) {
            step->action(expected_path);
        }
        auto end = Clock::now();
        if (step->opts.report_time) {
            blog("%s[step]%s %s%s%s completed in %s%.2fs%s\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset(),
                 c.cyan(), std::chrono::duration<double>(end - build_phase_start).count(), c.reset());
        } else if (!step->opts.silent) {
            blog("%s[step]%s %s%s%s completed\n", c.gray(), c.reset(), c.yellow(), step->opts.name.c_str(), c.reset());
        }
        step->completed() = true;
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

    std::vector<Path> completedInputs(Step* step) {
        std::vector<Path> res;
        for (auto* input : step->inputs) {
            if (!input->completed()) panic("Input step %s of step %s is not completed before dependant\n", input->opts.name.c_str(), step->opts.name.c_str());
            res.push_back(cacheEntryOfStep(input));
        }
        return res;
    }

    Path cacheEntryOfSourceDepfile(Hash h) {
        return cache / "deps" / (std::to_string(h.value));
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

void build(Build* b); // expected signature of build function

int main(int argc, char** argv) { // NOLINT
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    auto cache = root / Dir{".cache"}; std::filesystem::create_directories(cache, ec);
    auto out = root / Dir{"build"}; std::filesystem::create_directories(out, ec);
    auto self_source = root / Path{"build.cpp"};

    Build b{argc, argv, root, cache, out};
    try {
        build(&b);
    } catch (const std::exception& e) {
        panic("your build script exited with exception: %s\n", e.what());
    }
    try {
        b.runBuildPhase();
    } catch (const std::exception& e) {
        panic("build phase exited with exception: %s\n", e.what());
    }
    return 0;
}
