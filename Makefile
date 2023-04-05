CC=g++
CFLAGS=-std=c++17 -Wall -Wextra -Wpedantic
BUILD_DIR=build
TARGET=simple_proxy_server
SRC=$(shell find ./src -type f -name *.cpp)
PORT=7331

.PHONY: all build run clean kill

all: kill clean build run

build: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

run:
	./$(BUILD_DIR)/$(TARGET)

clean:
	-rm -rf $(BUILD_DIR)

kill:
	lsof -t -i tcp:$(PORT) | xargs kill

