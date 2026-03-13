build: debug
	gcc src/main.c -o target/debug/csh -lm
build-release: release
	gcc -O4 src/main.c -o target/release/csh -lm
run: build
	./target/debug/csh
run-release: build-release
	./target/release/csh
debug:
	mkdir -p target/debug
release:
	mkdir -p target/release
fmt:
	clang-format -i src/main.c
clean:
	rm -rf target