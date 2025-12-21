.PHONY: clean
clean:
	./foreach_howto.sh rm -rf .cache build b

.PHONY: bootstrap
bootstrap:
	./foreach_howto.sh ${CXX} build.cpp -o b

.PHONY: install-all
install-all:
	./foreach_howto.sh ./b install
