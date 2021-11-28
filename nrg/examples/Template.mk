CC := g++

CFLAGS := -std=c++17
CFLAGS += -Wall -Wextra -Wno-unknown-pragmas -Wpedantic
CFLAGS += -fPIE -g
CFLAGS += -I../../include
CFLAGS += -I../../../include
CFLAGS += -I../../../lib/expected/include

LDFLAGS := -L../../lib
LDFLAGS += -lnrg -Wl,-rpath='$$ORIGIN/../../lib'

SRC := main.cpp
OBJ := main.o
TARGET := main.out

default: $(TARGET)

$(OBJ): $(SRC) ../common/exception.hpp
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJ)
