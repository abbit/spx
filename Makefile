CC=g++
CFLAGS=-std=c++17 -I vendor
# for Solaris
# CFLAGS=-lnsl -lsocket -lresolv -std=c++17

SRC=src/main.cpp src/server.cpp
OUT=build/out

$(OUT): dir $(SRC)
	$(CC) $(SRC) -o $(OUT) $(CFLAGS)

dir:
	-mkdir build