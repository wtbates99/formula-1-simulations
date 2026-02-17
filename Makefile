BUILD_DIR ?= build
CMAKE_FLAGS ?=

.PHONY: all configure build cli viewer ingest clean legacy

all: build

configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) -j

cli: build
	$(BUILD_DIR)/f1_cli

viewer: build
	$(BUILD_DIR)/sim_viewer

ingest: build
	$(BUILD_DIR)/f1_cli

legacy:
	mkdir -p build
	g++ -O2 -Wall -Wextra -std=c++17 -Iinclude main.cpp -o build/f1_cli -lsqlite3 -lcurl

clean:
	rm -rf $(BUILD_DIR) build