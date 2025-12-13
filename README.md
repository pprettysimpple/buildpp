### My build tooling

Many thanks to nob.h and build.zig for inspiration. Without this projects at my hands I would never be able to create this tool.

First idea is that you must be able to build c++ sources using only c++ compiler.
Second idea comes from first. Write some simple tooling to assist you in creating configuration for your project in c++.

Process of building with this tool:
You write your `build.cpp` file. You compile it. Then you call it and it acts like your build tool (cmake, make, ninja).
If it detects that it's executable is outdated, tool will automatically rebuild itself and you would only notice it by seeing how it slows down for a few seconds.

I've prepared a basic guide on how to get started with it down here:

```bash
# install compiler that supports c++20. example will use global clang++
sudo apt install clang # probably, you already have one
git clone git@github.com:pprettysimpple/buildpp.git
cd buildpp/example_01_simple
clang++ build.cpp -o b # bootstrapping
# after you successfully built yout first example,
#   check out what's written in build.cpp and try tackle with it for a bit
# then go on to your next example

# to recompile after any change:
./b <step-name>

./b build # will launch `build` step
```

You would pick-up pace real easy if you see example folders. Start from first one. No hard stuff here at all.
