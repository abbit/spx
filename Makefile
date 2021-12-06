CC=g++
CFLAGS=-std=c++17 -I vendor
# for Solaris
# CFLAGS=-lnsl -lsocket -lresolv -std=c++17 -I vendor

SRC=src/main.cpp src/server.cpp
OUT=build/out

$(OUT): kill clean dir $(SRC)
	$(CC) $(SRC) -o $(OUT) $(CFLAGS)

dir:
	-mkdir build

clean:
	-rm -rf build

kill:
	lsof -t -i tcp:7331 | xargs kill

run:
	$(OUT)

.PHONY: dir clean kill run