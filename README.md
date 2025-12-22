### C++ header-only build tooling

#### Idea
I believe You must be able to build c++ project using only c++ compiler and no other dependencies.

#### How does it look to write a build script?
Link to [how_to](https://github.com/pprettysimpple/buildpp/blob/master/how_to/01_simple/build.cpp) entry
```cpp
#define BPP_RECOMPILE_SELF_CMD "clang++"
#include "buildpp.h"

void configure(Build* b) {
    auto main = b->addExe({.name = "main", .desc = "My simple executable"}, {"main.cpp"});
    b->installExe(main);
    b->addRunExe(main, {.name = "run", .desc = "Run the main executable", .args = b->cli_args});

    auto libmain = b->addLib({.name = "main", .desc = "My simple library"}, {"main.cpp"});
    b->installLib(libmain);
}
```

#### What does it do?
You write your `build.cpp` file. You compile it ONCE in the simplest way possible (aka `clang++ -o b build.cpp`). Then you call it and it acts like your build tool (cmake, make, ninja).
If it detects that it's executable is outdated, tool will automatically rebuild itself and you would only notice it by seeing how it slows down for a few seconds and reports recompilation time.

Here is some bash to get started:
```bash
sudo apt install clang # install compiler
git clone git@github.com:pprettysimpple/buildpp.git
cd buildpp/how_to/01_simple
clang++ build.cpp -o b # bootstrap
./b help
```

Follow the `how_to` folder examples to get yourself comfortable with writing this scripts

#### Thank you!
Many thanks to [nob.h](https://github.com/tsoding/nob.h) and build.zig for inspiration. Without this projects at my hands I would never be able to create this tool.
