### Build system for prosperity

```bash
# install compiler that supports c++20. example will use global clang++
sudo apt install clang # probably, you already have one
git clone git@github.com:pprettysimpple/buildpp.git
cd buildpp/examples
clang++ -g -std=c++20 build.cpp -o b && ./b -h # bootstrap
# after bootstrap, read content of example build.cpp

# to recompile after any change:
./b <step-name>

./b # will launch `build` step implicitly
```