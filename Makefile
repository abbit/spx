CC=g++
CFLAGS=-std=c++17 -I vendor -Wall -Wextra -pedantic
# for Solaris
# CFLAGS=-lnsl -lsocket -lresolv -std=c++17 -I vendor

ROOT_DIR=/Users/abbit/dev/university/os/spx
TARGET=$(ROOT_DIR)/build/out
SRC=$(shell find $(ROOT_DIR)/src -type f -name *.cpp)
PORT=7331

all: kill clean dir $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

dir:
	-mkdir build

clean:
	-rm -rf build

kill:
	lsof -t -i tcp:$(PORT) | xargs kill

.PHONY: dir clean kill all