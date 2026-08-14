# HEX4-Truth C 核心库 Makefile
# 用法: make / make test / make lib / make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
AR      = ar rcs

LIB_SRCS = hex4_core.c hex4_tc_propagator.c
LIB_OBJS = $(LIB_SRCS:.c=.o)
LIB_NAME = libhex4_truth.a
TEST_SRC = test_hex4_core.c
TEST_BIN = test_hex4_core

.PHONY: all lib test clean

all: test

lib: $(LIB_NAME)

$(LIB_NAME): $(LIB_OBJS)
	$(AR) $@ $^

%.o: %.c hex4_core.h
	$(CC) $(CFLAGS) -c -o $@ $<

hex4_tc_propagator.o: hex4_tc_propagator.c hex4_tc_propagator.h

$(TEST_BIN): $(TEST_SRC) $(LIB_NAME)
	$(CC) $(CFLAGS) -o $@ $< -L. -lhex4_truth

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(LIB_OBJS) $(LIB_NAME) $(TEST_BIN)
