CXX      = g++
CXXFLAGS = -O2 -Wall -Wextra -std=c++17
LDFLAGS  = -lsqlite3

SRC    = main.cpp

all: build/f1

build/f1: $(SRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o build/f1 $(LDFLAGS)

clean:
	rm -rf build