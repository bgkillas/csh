build: debug
	gcc src/main.c -o target/debug/csh -lreadline
build-release: release
	gcc -O4 src/main.c -o target/release/csh -lreadline
run: build
	./target/debug/csh
run-release: build-release
	./target/release/csh
debug:
	mkdir -p target/debug
release:
	mkdir -p target/release
fmt:
	clang-format -i src/main.c --sort-includes=false --style="{IndentWidth: 4}"
clean:
	rm -rf target
