build-lib:
	cd build && cmake .. && make -j$(nproc)

build-app: build-lib
	gcc -o run_wasm run_wasm.c -I source -L build/source -lm3 -lm \
	    -Dd_m3HasTracer -Dd_m3HasWASI -DDEBUG=1 -g -O0

run: build-app
	./run_wasm test/lang/fib32.wasm fib 10

# --- sparse memory unit tests -----------------------------------------------

TEST_SPARSE_SRC   = test/internal/test_sparse_memory.c source/m3_sparse_memory.c source/m3_core.c
TEST_SPARSE_BIN   = build/test_sparse_memory
TEST_SPARSE_FLAGS = -I source -lm

build-test-sparse:
	$(CC) -o $(TEST_SPARSE_BIN) $(TEST_SPARSE_SRC) $(TEST_SPARSE_FLAGS)

test-sparse: build-test-sparse
	$(TEST_SPARSE_BIN)

test: test-sparse
