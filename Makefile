# Makefile for LRU Cache Capstone Project

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

# Output binaries
SERVER_BIN = build/server.exe
TEST_BIN = build/test_lru.exe

# Ensure build directory exists (Windows)
# If mkdir fails because it exists, we ignore the error
all: build_dir $(SERVER_BIN) $(TEST_BIN)

build_dir:
	@if not exist build mkdir build

# Compile the HTTP server
# Needs ws2_32.lib for Winsock; the #pragma comment in code handles this on MSVC,
# but for MinGW/GCC we pass -lws2_32 explicitly.
$(SERVER_BIN): src/server.c src/lru_cache.c include/lru_cache.h
	$(CC) $(CFLAGS) src/server.c src/lru_cache.c -o $@ -lws2_32

# Compile the test suite
# test_lru.c #includes lru_cache.c directly, so we don't compile it separately here
$(TEST_BIN): tests/test_lru.c include/lru_cache.h
	$(CC) $(CFLAGS) tests/test_lru.c -o $@

# Run tests
test: $(TEST_BIN)
	$(TEST_BIN)

# Run server
run: $(SERVER_BIN)
	$(SERVER_BIN)

clean:
	@if exist build\server.exe del build\server.exe
	@if exist build\test_lru.exe del build\test_lru.exe
	@if exist build rmdir build

.PHONY: all build_dir test run clean
