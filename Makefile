.PHONY: clean
clean:
	./foreach_example.sh rm -rf .cache build b

.PHONY: bootstrap
bootstrap:
	./foreach_example.sh ${CXX} build.cpp -o b

.PHONY: install-all
install-all:
	./foreach_example.sh ./b install
