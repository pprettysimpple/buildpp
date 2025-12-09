// header that build.cpp includes
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <array>
#include <cstdint>
#include <sstream>

#include <unistd.h>

struct Hash {
    uint64_t value = 0;

    [[nodiscard]] Hash combine(const Hash& other) const {
        uint64_t combined = 14695981039346656037ULL;
        combined ^= value;
        combined *= 1099511628211ULL;
        combined ^= other.value;
        combined *= 1099511628211ULL;
        return Hash{combined};
    }

    [[nodiscard]] Hash combineUnordered(const Hash& other) const {
        // order-independent combine
        return Hash{value ^ other.value};
    }
};

struct Option {
    std::string key;
    std::string description = "";
};

#ifndef RECOMPILE_SELF_CMD
#error R"(To use this library you need to setup how this script will be compiled This is done through this macro (where error is emited). Try to define it just before including header as follows: clang++ -g -std=c++20 build.cpp)"
#endif
#ifndef RECOMPILE_SELF_HASH
#define RECOMPILE_SELF_HASH 0
#endif
#ifndef RECOMPILE_SELF_OPTIONS
#define RECOMPILE_SELF_OPTIONS std::array<Option,0>{}
#endif
constexpr std::string_view self_compile_command = RECOMPILE_SELF_CMD;
constexpr Hash self_hash = {RECOMPILE_SELF_HASH};
static auto old_options = RECOMPILE_SELF_OPTIONS;

using Path = std::filesystem::path;
using Dir = std::filesystem::path;

struct Step {
    struct Options {
        std::string name;
        std::string desc;
        bool phony{false};
        bool silent{false};
    } opts;

    std::vector<Step*> dependencies = {};
    std::vector<Step*> dependants = {};

    std::function<Hash(Hash)> scan_deps = nullptr; // what this step depends other than other steps
    std::function<void(std::filesystem::path/* write_artifact_here*/)> action = nullptr;
    size_t idx;

    // NOTE: Step is considered up-to-date if its hash matches combined hash of dependencies
    //       Each entity that can affect the step's outcome should contribute to the hash by itself
    //       target when compiling should consider it's sources, flags, etc and return combined hash from scan_deps
    Hash hash{0}; // 0 == not computed
    std::unique_ptr<std::atomic<bool>> completed_impl = std::make_unique<std::atomic<bool>>(false);

    void dependOn(Step* step) {
        dependencies.push_back(step);
        step->dependants.push_back(this);
    }

    std::atomic<bool>& completed() { // to avoid writing buggy if(step->completed)
        return *completed_impl;
    }
};

struct LazyPath {
    Step* step;
};

struct Target {
    enum class Type {
        Exe,
        Object,
    };

    struct Options {
        std::string name;
        std::string desc = "";
        Type type;

        std::string command;

        std::vector<Path> sources = {};
    };

    Options opts;
    Step* step;
};

struct Run {
    struct Options {
        std::optional<std::string> name = std::nullopt; // optional name for this run step
        std::optional<std::string> desc = std::nullopt; // optional working directory
        Path working_dir = Path{"."}; // working directory to run in
        std::vector<std::string> args = {}; // arguments to pass to the executable
    };

    Target* target; // executable to run on
    Step* step;
    Options opts;
};

Hash inline stableHashFile(Path path) {
    auto* fin = std::fopen(path.c_str(), "rb");
    if (!fin) {
        return {0};
    }

    Hash hash{};
    int c = 0;
    while ((c = std::fgetc(fin)) != EOF) {
        hash = hash.combine(Hash{static_cast<uint64_t>(c)});
    }
    return hash;
}

Hash inline stableHashStr(std::string_view str) {
    Hash hash{};
    for (char c : str) {
        hash = hash.combine(Hash{static_cast<uint64_t>(c)});
    }
    return hash;
}

// main structure build script will operate on
struct Build {
    friend struct MainBuilderFriend;
private:
int saved_argc;
    std::vector<char*> saved_argv;
    std::vector<std::string> requested_steps;
    bool verbose = false;
    int max_parallel_jobs = -1;
    // int max_rss_mb = -1; // TODO: allow limit max memory usage

    Dir root;
    Dir cache;

    std::vector<std::optional<std::string>> parsed_options; // order same as old_options
    std::vector<Option> options; // parsed from cli/config on rebuild
    std::list<Target> targets;
    std::list<Run> runs;
    std::list<Step> steps;
    std::vector<bool> step_completed;
    std::vector<std::pair<Step*, Path>> install_list;
    Step* install_step = nullptr;
    Step* build_all_step = nullptr;

    std::mutex queue_mutex;
    std::vector<Step*> steps_run_order; // popped from back
    std::vector<std::thread> worker_threads;
public:
    Dir out;
    std::vector<std::string> cli_args;

    Build(Dir root, Dir cache, Dir out) : root(root), cache(cache), out(out), options(old_options.begin(), old_options.end()) {
        steps.push_back(Step{.opts = {.name = "install", .desc = "Install targets", .silent = false}, .idx = steps.size()});
        install_step = &steps.back();
        
        steps.push_back(Step{.opts = {.name = "build", .desc = "Build all targets", .silent = false}, .idx = steps.size()});
        build_all_step = &steps.back();
        build_all_step->dependOn(install_step);
        
        steps.push_back(Step{.opts = {.name = "list", .desc = "List available steps", .silent = false}, .idx = steps.size()});
        steps.back().action = [this](std::filesystem::path /*phony*/) {
            printf("Available steps:\n");
            for (const auto& step : steps) {
                if (!step.opts.silent) printf("- %s: %s\n", step.opts.name.c_str(), step.opts.desc.c_str());
            }
        };
        
        steps.push_back(Step{.opts = {.name = "help", .desc = "Show help message", .silent = false}, .idx = steps.size()});
        steps.back().action = [this](std::filesystem::path /*phony*/) {
            printf("Build tool help:\n");
            printf("Usage: %s [options] [steps] [-- run-args]\n", saved_argv[0]);
            printf("Options:\n");
            for (const auto& opt : options) {
                printf("  -D%s :: %s\n", opt.key.c_str(), opt.description.c_str());
            }
            printf("Steps:\n");
            for (const auto& step : steps) {
                if (!step.opts.silent) printf("  %s :: %s\n", step.opts.name.c_str(), step.opts.desc.c_str());
            }
            printf("Executables:\n");
            for (const auto& run : runs) {
                printf("  run-%s :: Run exe %s\n", run.target->opts.name.c_str(), run.target->opts.name.c_str());
            }
        };
    }

    [[noreturn]] void panic(const char* fmt, ...) {
        fflush(NULL);
        fprintf(stderr, "buildpp: ");
        if (isatty(fileno(stderr))) fprintf(stderr, "\033[1;31m"); // red
        fprintf(stderr, "error: ");
        if (isatty(fileno(stderr))) fprintf(stderr, "\033[0m"); // reset
        if (isatty(fileno(stderr))) fprintf(stderr, "\033[1m"); // bold
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        exit(1);
    }

    template <typename T>
    std::optional<T> option(std::string_view key, std::string_view description = "") {
        // check if the same option exists in old options
        bool exist_in_old = false;
        size_t idx = 0;
        for (const auto& opt : old_options) {
            if (opt.key == key) {
                exist_in_old = true;
                break;
            }
            ++idx;
        }
        bool exist_in_new = false;
        for (const auto& opt : options) {
            if (opt.key == key) {
                exist_in_new = true;
                break;
            }
        }

        if (!exist_in_new) {
            options.push_back(Option{.key = std::string(key), .description = std::string(description)});
        }

        if (!exist_in_old) { // we need to recompile self with new option
            fputs(("buildpp: New option " + std::string(key) + " " + std::string(description) + " detected\n").data(), stderr);
            recompileSelf(calcSelfHash());
        }

        if (!parsed_options.at(idx).has_value()) {
            return std::nullopt;
        }

        try {
            if constexpr (std::is_same_v<T, bool>) {
                auto val_str = parsed_options.at(idx).value();
                if (val_str == "1" || val_str == "true" || val_str == "yes") {
                    return true;
                } else if (val_str == "0" || val_str == "false" || val_str == "no") {
                    return false;
                } else {
                    panic("Invalid boolean option value for key: \"%s\" value is \"%s\"\n", key.data(), val_str.data());
                }
            }
            if constexpr (std::is_same_v<T, std::string>) {
                return parsed_options.at(idx).value();
            }

            // best-effort parsing for other types using standard streams
            T value;
            std::istringstream iss{parsed_options.at(idx).value()};
            iss >> value;
            return value;
        } catch (...) {
            panic("Failed to parse option value for key: \"%s\" value is \"%s\"\n", key.data(), parsed_options.at(idx).value().data());
        }
    }

    Target* addTarget(Target::Options opts) {
        steps.push_back(Step{.opts = {.name = "build-" + opts.name, .desc = opts.desc, .silent = false}, .idx = steps.size()});
        targets.push_back(Target{
            .opts = opts,
            .step = &steps.back(),
        });
        auto t = &targets.back();
        std::vector<Target*> objs;
        if (opts.type != Target::Type::Object) {
            for (auto src : t->opts.sources) {
                auto obj = addTarget(Target::Options{
                    .name = src.filename().replace_extension(".o"),
                    .desc = "Object file for " + src.string(),
                    .type = Target::Type::Object,
                    .command = opts.command,
                    .sources = { src },
                });
                objs.push_back(obj);
                t->step->dependOn(obj->step);
            }
        }
        build_all_step->dependOn(t->step);

        t->step->scan_deps = [t](Hash h) {
            // calculate hash based on target options and source files
            h = h.combine(stableHashStr(t->opts.name));
            h = h.combine(Hash{static_cast<uint64_t>(t->opts.type)});
            h = h.combine(stableHashStr(t->opts.command));
            if (t->opts.type == Target::Type::Object) {
                for (auto src : t->opts.sources) h = h.combine(stableHashFile(src));
            }
            return h;
        };

        t->step->action = [t, this](std::filesystem::path out) { // action is invoked when step must be performed
            auto cmd = targetPrepareFinalCommand(t, out);

            if (verbose)
                fprintf(stderr, "Compile command: %s\n", cmd.data());

            auto ret = std::system(cmd.data());
            if (ret != 0) panic("Failed to build target: %s\n", t->opts.name.c_str());
        };
        return t;
    }

    Run* addTargetRun(Target* target, Run::Options opts) {
        steps.push_back(Step{
            .opts = {
                .name = opts.name.value_or("run-" + target->opts.name),
                .desc = opts.desc.value_or("Run this exe"),
                .phony = true,
                .silent = false,
            },
            .idx = steps.size(),
        });
        runs.push_back(Run{
            .target = target,
            .step = &steps.back(),
            .opts = opts,
        });
        auto run = &runs.back();
        run->step->dependOn(target->step);
        run->step->action = [run, this](std::filesystem::path /*phony*/) {
            // invoke the built executable with args
            auto cmd = std::string{};
            cmd += "pushd " + run->opts.working_dir.string() + " > /dev/null && ";
            cmd += resolveLazyPath(LazyPath{run->target->step}).string() + " ";
            for (const auto& arg : run->opts.args) {
                cmd += arg + " ";
            }
            cmd += "&& popd > /dev/null";
            auto ret = std::system(cmd.data());
            if (ret != 0) {
                panic("Failed to run target: %s\n", run->target->opts.name.c_str());
            }
        };
        return run;
    }

    Step* install(Step* step, Path dest) {
        steps.push_back(Step{.opts = {.name = "install-" + step->opts.name, .desc = "Installs " + step->opts.name, .silent = false}, .idx = steps.size()});
        auto file_install_step = &steps.back();
        file_install_step->dependOn(step);
        install_step->dependOn(file_install_step);
        file_install_step->scan_deps = [this, step, dest](Hash h) {
            auto child_path = resolveLazyPath(LazyPath{step});
            // check if destination file exists and equals to target artifact
            if (!std::filesystem::exists(out / dest)) return Hash{0};
            auto dest_hash = stableHashFile(out / dest);
            auto src_hash = stableHashFile(child_path);
            return dest_hash.value == src_hash.value ? h : Hash{0};
        };
        file_install_step->action = [this, step, dest, file_install_step](std::filesystem::path) {
            printf("[%s] target %s -> %s\n", file_install_step->opts.name.c_str(), step->opts.name.c_str(), dest.c_str());
            // copy file from artifact cache to dest.path
            // but first ensure parent directory exists
            std::filesystem::create_directories((out / dest).parent_path());
            std::filesystem::copy_file(resolveLazyPath(LazyPath{step}),
                                    out / dest,
                                    std::filesystem::copy_options::overwrite_existing);
        };
        return file_install_step;
    }

    Step* addStep(Step::Options opts) {
        steps.push_back(Step{
            .opts = opts,
            .idx = steps.size(),
        });
        return &steps.back();
    }

    void generateCompileCommandsJson(Dir out_dir) {
        // walk targets, prep json
        std::vector<std::string> cmds;
        for (auto& t : targets) {
            if (t.opts.type != Target::Type::Object || t.opts.sources.size() != 1) continue;
            auto command = targetPrepareFinalCommand(&t, {});
            auto file = (root / t.opts.sources[0]).string();
            auto directory = std::string{"."};
            
            auto cmd = std::string{};
            cmd += "  {\n";
            cmd += "    \"command\":\"" + command + "\""; cmd += ",\n";
            cmd += "    \"file\":\"" + file + "\""; cmd += ",\n";
            cmd += "    \"directory\":\"" + directory + "\"\n";
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
        std::filesystem::create_directories(out_dir);
        auto db_path = out_dir / "compile_commands.json";
        auto _ = std::system(("rm -rf " + db_path.string()).c_str());
        std::ofstream out{db_path};
        if (!out.is_open()) panic("Can't open file %s\n", db_path.c_str());
        out << res;
    }

private:

    std::string
    targetPrepareFinalCommand(Target* t, Path out) {
        auto cmd = t->opts.command + " ";
        if (!out.empty()) cmd += "-o " + out.string() + " ";

        if (t->opts.type == Target::Type::Object) {
            cmd += "-c ";
            if (t->opts.sources.size() > 1) panic("split object-targets into one target per-file. multiple is not supported");
            cmd += t->opts.sources[0].string() + " ";
        } else {
            for (auto src : t->opts.sources) cmd += src.string() + " ";
        }

        return cmd;
    }

    void runRequestedSteps() {
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

        std::vector<bool> step_visited(steps.size(), false);
        std::function<void(size_t)> visit = [&](size_t pos) {
            if (step_visited[pos]) return;
            step_visited[pos] = true;
            steps_run_order.push_back(all_steps_flat[pos]);
            for (auto* dep : all_steps_flat[pos]->dependencies) {
                visit(dep->idx);
            }
        };

        for (size_t i = 0; i < steps_to_perform.size(); i++) {
            visit(steps_to_perform[steps_to_perform.size() - i - 1]);
        }
        std::reverse(steps_run_order.begin(), steps_run_order.end());

        for (size_t i = 0; i < max_parallel_jobs; i++) {
            worker_threads.emplace_back([this]() {
                try {
                    while (true) {
                        Step* step = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex);
                            if (steps_run_order.empty()) return;
                            step = steps_run_order.back();
                            steps_run_order.pop_back();
                        }
                        for (auto dep : step->dependencies) {
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
    }

    void saveArgs(int argc, char** argv) {
        saved_argc = argc;
        saved_argv.resize(argc + 1);
        for (int i = 0; i < argc; ++i) {
            saved_argv[i] = argv[i];
        }
        saved_argv[argc] = nullptr;
    }

    void parseArgs() {
        auto argc = saved_argc;
        auto argv = saved_argv.data();
        parsed_options.resize(old_options.size());
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--") { // the rest are the args for the run
                cli_args = std::vector<std::string>(argv + i + 1, argv + argc);
                break;
            }

            // options
            if (arg.find("-D", 0) == 0) {
                std::string key = arg.substr(2);
                size_t idx = 0;
                for (const auto& opt : old_options) {
                    if (opt.key + "=" == key.substr(0, opt.key.size() + 1)) { // key=value
                        std::string value = key.substr(opt.key.size() + 1);
                        parsed_options[idx] = value;
                        break;
                    }
                    if (opt.key == key) { // key only, treat as boolean true
                        parsed_options[idx] = "true";
                        break;
                    }
                    ++idx;
                }
                continue;
            }

            // builtin options
            if (arg == "-h" || arg == "--help") {
                requested_steps.push_back("help");
                continue;
            }

            if (arg == "-v" || arg == "--verbose") {
                verbose = true;
                continue;
            }

            if (arg == "-j" || arg == "--jobs") {
                if (i + 1 >= argc) {
                    panic("Expected number of jobs after %s\n", arg.data());
                }
                max_parallel_jobs = std::stoi(argv[++i]);
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

    Hash calcSelfHash() {
        auto start = std::chrono::high_resolution_clock::now();
        // scan deps using compiler on ourselfs using -MD
        auto cmd = std::string{};
        cmd += self_compile_command;
        cmd += " -E -MD > /dev/null";
        auto ret = std::system(cmd.data());
        if (ret != 0) panic("Failed to scan build tool dependencies\n");

        // parse generated .d file
        auto dep_file = std::ifstream{"build.d"};
        if (!dep_file.is_open()) {
            panic("Failed to open build.d for reading\n");
        }
        // skip until ": "
        while (dep_file.peek() != ':') {
            dep_file.get();
        }
        dep_file.get(); // skip ':'
        dep_file.get(); // skip ' '
        std::vector<std::filesystem::path> dep_files;
        std::string file;
        while (dep_file >> file) {
            // Handle line continuation '\'
            if (file.back() == '\\') {
                file.pop_back();
            }
            dep_files.push_back(file);
        }

        dep_file.close();
        std::error_code ec;
        std::filesystem::remove("build.d", ec);
        auto deps_collected = std::chrono::high_resolution_clock::now();
        // calculate combined hash
        Hash h{};
        for (const auto& dep_path : dep_files) {
            h = h.combine(stableHashFile(Path{dep_path.c_str()}));
        }
        auto end = std::chrono::high_resolution_clock::now();

        // fprintf(stderr, "Self-hash calculation took %.2f ms. deps collected in %.2f ms\n",
        //         std::chrono::duration<double, std::milli>(end - deps_collected).count(),
        //         std::chrono::duration<double, std::milli>(deps_collected - start).count()); 

        return h;
    }
 
    void checkIfBuildScriptChanged() {
        auto new_hash = calcSelfHash();
        if (self_hash.value != new_hash.value) {
            // fprintf(stderr, "buildpp: Build tool source changed, recompiling...\n");
            recompileSelf(new_hash);
        }
    }

    void recompileSelf(Hash self_hash) {
        auto start = std::chrono::high_resolution_clock::now();
        auto escape = [](std::string_view str) -> std::string {
            std::string escaped;
            for (char c : str) {
                if (c == '\"' || c == '\\') {
                    escaped += '\\';
                }
                escaped += c;
            }
            return escaped;
        };
        // std::cerr << "Recompiling build tool...\n";
        auto compile = std::string{};
        compile += self_compile_command;
        compile += " -DRECOMPILE_SELF_HASH=" + std::to_string(self_hash.value) + "ULL";
        compile += " -DRECOMPILE_SELF_OPTIONS=\"std::array<Option," + std::to_string(options.size()) + ">{";
        for (size_t i = 0; i < options.size(); ++i) {
            compile += "Option{.key=\\\"" + escape(options[i].key) + "\\\", .description=\\\"" + escape(options[i].description) + "\\\"},";
        }
        compile += "}\"";
        // base command with compiler and user flags walks through entiere lifetime of script
        compile += " -DRECOMPILE_SELF_CMD=\"\\\"" + escape(self_compile_command) + "\\\"\"";
        auto name = std::string{saved_argv[0]};
        auto fname_pos = name.find_last_of('/');
        if (fname_pos != std::string::npos) {
            name = name.substr(fname_pos + 1);
        } else {
            name = name;
        }
        compile += " -o " + name + "";
        printf("[*] Recompiling build tool\n");
        auto ret = std::system(compile.data());

        if (ret != 0) panic("Failed to recompile build tool\n");
        // move cursor up one line to overwrite the recompilation message
        if (isatty(fileno(stdout))) {
            printf("\033[A"); // ANSI escape code to move cursor up one line
            printf("\033[2K"); // ANSI escape code to clear the entire line
        }
        auto end = std::chrono::high_resolution_clock::now();
        printf("[+] Recompiled build tool in %.2f s\n", std::chrono::duration<double>(end - start).count());

        // execv to replace current process 
        execv(name.c_str(), saved_argv.data());
        // if execv returns, it failed 
        panic("Failed to exec recompiled build tool\n");
    }

    void performStepIfNeeded(Step* step) {
        if (step->completed()) {
            return;
        }

        // perform dependencies first
        for (auto* dep : step->dependencies) {
            assert(dep->completed() && "Dependency step not completed before dependant");
        }

        // recalc hash
        Hash h{0};
        for (auto* dep : step->dependencies) {
            h = h.combineUnordered(dep->hash);
        }
        if (step->scan_deps) {
            h = step->scan_deps(h);
        }
        step->hash = h;
        auto expected_path = cache / std::to_string(step->hash.value);

        if (!step->opts.phony) {
            // check if we already have an artifact with the same hash
            if (std::filesystem::exists(expected_path)) {
                if (!step->opts.silent || verbose) fprintf(stdout, "[step] \"%s\" up-to-date!\n", step->opts.name.c_str());
                step->completed() = true;
                return;
            }
        }

        // perform this step
        if (!step->opts.silent || verbose) fprintf(stdout, "[step] building \"%s\"...\n", step->opts.name.c_str());
    
        if (step->action) {
            step->action(expected_path);
        }
        step->completed() = true;
    }

    std::filesystem::path resolveLazyPath(LazyPath lp) {
        assert(lp.step->completed() && "LazyPath step not completed before resolving path");
        return cache / std::to_string(lp.step->hash.value);
    }
};

struct MainBuilderFriend { // used to avoid flooding user with implementation details like "perform..." but you can stll call them, if you want to, through this proxy
    Build* b;

    void startup(int argc, char** argv) {
        b->saveArgs(argc, argv);
        b->checkIfBuildScriptChanged();
        b->parseArgs();
    }

    void runRequestedSteps() {
        b->runRequestedSteps();
    }
};

void build(Build* b); // expected signature of build function

int main(int argc, char** argv) { // NOLINT
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    auto cache = cwd / Dir{".cache"}; std::filesystem::create_directories(cache, ec);
    auto out = cwd / Dir{"build"}; std::filesystem::create_directories(out, ec);
    auto self_source = cwd / Path{"build.cpp"};

    Build b{cwd, cache, out};
    MainBuilderFriend bf{&b};
    bf.startup(argc, argv);
    build(&b);
    bf.runRequestedSteps();
    return 0;
}
