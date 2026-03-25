build: debug
	gcc -Wall src/main.c src/parse.c src/run.c -o target/debug/csh -lreadline -g
build-release: release
	gcc -Wall src/main.c src/parse.c src/run.c -o target/release/csh -lreadline -O4
run: build
	./target/debug/csh
run-release: build-release
	./target/release/csh
debug:
	mkdir -p target/debug
release:
	mkdir -p target/release
fmt:
	clang-format -i --sort-includes=false --style="{IndentWidth: 4}" src/*
clean:
	rm -rf target
