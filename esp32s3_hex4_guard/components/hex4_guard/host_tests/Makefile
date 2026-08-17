# hex4_guard host 单元测试 (零外部依赖; cJSON 引用 IDF 自带源码)
# 用法: make / make test / make clean

CC       = gcc
IDF_PATH ?= /home/groot/esp/esp-idf
CJSON_DIR = $(IDF_PATH)/components/json/cJSON
CFLAGS   = -Wall -Wextra -O2 -std=c99 -I.. -I$(CJSON_DIR)

# M1.1 帧协议层
PROTO_SRCS = ../guard_cmd.c ../guard_frame.c ../guard_replay.c
# M1.2 判定链层 (编排层 hex4_guard 依赖 FreeRTOS, 板级对拍验证)
JUDGE_SRCS = ../guard_cmd.c ../guard_permissions.c ../guard_verify.c \
             ../guard_policy.c ../guard_reply.c $(CJSON_DIR)/cJSON.c

.PHONY: all test clean

all: test

test_guard_protocol: test_guard_protocol.c $(PROTO_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test_guard_judge: test_guard_judge.c $(JUDGE_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: test_guard_protocol test_guard_judge
	./test_guard_protocol && ./test_guard_judge

clean:
	rm -f test_guard_protocol test_guard_judge
