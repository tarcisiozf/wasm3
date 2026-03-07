build-lib:
	cd build && cmake .. && make -j$(nproc)

build-app: build-lib
	gcc -o run_wasm run_wasm.c -I source -L build/source -lm3 -lm

run: build-app
	./run_wasm test/lang/fib32.wasm fib 10